package com.findlk.app

import android.annotation.SuppressLint
import android.app.Notification
import android.app.Service
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat

/**
 * Mantém a conexão com a tag enquanto a proteção está ligada: heartbeat a cada 5 s,
 * leitura de RSSI, notificações do Estado e alarme local quando a conexão cai.
 * Todas as operações GATT passam por uma fila (o Android só aceita uma por vez).
 */
@SuppressLint("MissingPermission") // a tela só liga a proteção depois das permissões concedidas
class TagService : Service() {

    companion object {
        const val ACTION_START = "start"
        const val ACTION_STOP = "stop"
        const val ACTION_COMING = "coming"
        const val ACTION_FOUND = "found"
        const val ACTION_DISARM = "disarm"
        const val ACTION_SEND_CONFIG = "config"
        const val ACTION_STOP_ALARM = "stop_alarm"

        fun send(context: Context, action: String) {
            val intent = Intent(context, TagService::class.java).setAction(action)
            if (action == ACTION_START) context.startForegroundService(intent) else context.startService(intent)
        }
    }

    private val handler = Handler(Looper.getMainLooper())
    private lateinit var prefs: Prefs
    private var gatt: BluetoothGatt? = null
    private var ready = false
    private var wakeLock: PowerManager.WakeLock? = null
    private var smoothedRssi: Double? = null

    /* A conexão só fica autenticada depois da primeira escrita protegida (o Android criptografa
     * sob demanda). Configuração e leitura do estado são refeitas após o primeiro heartbeat aceito. */
    private var authenticated = false
    private var configSent = false

    /* ---------- Fila de operações GATT ---------- */

    private val queue = ArrayDeque<(BluetoothGatt) -> Boolean>()
    private var busy = false

    private fun enqueue(op: (BluetoothGatt) -> Boolean) {
        handler.post {
            queue.addLast(op)
            next()
        }
    }

    private fun next() {
        val g = gatt ?: return
        if (busy || !ready) return
        val op = queue.removeFirstOrNull() ?: return
        busy = true
        if (!op(g)) {
            busy = false // não iniciou: segue para a próxima
            next()
        }
    }

    private fun done() {
        handler.post {
            busy = false
            next()
        }
    }

    private fun write(uuid: java.util.UUID, text: String) = enqueue { g ->
        val chr = g.getService(TagProtocol.SERVICE)?.getCharacteristic(uuid) ?: return@enqueue false
        val bytes = text.toByteArray(Charsets.UTF_8)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(chr, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            chr.value = bytes
            chr.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            g.writeCharacteristic(chr)
        }
    }

    /* ---------- Ciclo de vida ---------- */

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        prefs = Prefs(this)
        Alarm.createChannels(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START, null -> start()
            ACTION_STOP -> stopProtection()
            ACTION_COMING -> write(TagProtocol.COMING, prefs.name.ifBlank { "Responsável" })
            ACTION_FOUND -> {
                Alarm.stop(this)
                write(TagProtocol.COMMAND, TagProtocol.CMD_FOUND)
            }
            ACTION_DISARM -> write(TagProtocol.COMMAND, TagProtocol.CMD_DISARM)
            ACTION_SEND_CONFIG -> sendConfig()
            ACTION_STOP_ALARM -> Alarm.stop(this)
        }
        return START_STICKY
    }

    override fun onDestroy() {
        disconnect()
        super.onDestroy()
    }

    private fun foregroundNotification(text: String): Notification =
        NotificationCompat.Builder(this, Alarm.CHANNEL_SERVICE)
            .setSmallIcon(R.drawable.ic_stat)
            .setContentTitle("FindLK protegendo")
            .setContentText(text)
            .setContentIntent(Alarm.openAppIntent(this))
            .setOngoing(true)
            .build()

    private fun start() {
        val address = prefs.tagAddress
        if (address.isBlank()) {
            stopSelf()
            return
        }
        ServiceCompat.startForeground(
            this, Alarm.ID_SERVICE, foregroundNotification("Conectando à tag…"),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE else 0,
        )
        prefs.protecting = true
        TagRepository.update { it.copy(protecting = true, message = "Conectando à tag…") }

        // O heartbeat precisa continuar com a tela apagada
        if (wakeLock == null) {
            wakeLock = getSystemService(PowerManager::class.java)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "FindLK:protecao").apply { acquire() }
        }
        if (gatt == null) {
            val device: BluetoothDevice = getSystemService(BluetoothManager::class.java).adapter.getRemoteDevice(address)
            // autoConnect = true: o Android reconecta sozinho quando a tag volta ao alcance
            gatt = device.connectGatt(this, true, callback, BluetoothDevice.TRANSPORT_LE)
        }
        scheduleDisconnectAlarm()
        handler.removeCallbacks(heartbeat)
        handler.postDelayed(heartbeat, TagProtocol.HEARTBEAT_PERIOD_MS)
    }

    private fun stopProtection() {
        prefs.protecting = false
        Alarm.stop(this)
        disconnect()
        TagRepository.update { TagUiState(message = "Proteção desligada") }
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun disconnect() {
        handler.removeCallbacksAndMessages(null)
        queue.clear()
        busy = false
        ready = false
        gatt?.disconnect()
        gatt?.close()
        gatt = null
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
    }

    /* ---------- Heartbeat, RSSI e alarme por desconexão ---------- */

    private val heartbeat = object : Runnable {
        override fun run() {
            if (ready) {
                write(TagProtocol.HEARTBEAT, prefs.name.ifBlank { "Responsável" })
                enqueue { g -> g.readRemoteRssi() }
            }
            handler.postDelayed(this, TagProtocol.HEARTBEAT_PERIOD_MS)
        }
    }

    private val disconnectAlarm = Runnable {
        if (prefs.protecting && !ready) {
            Alarm.start(this, "Sinal da tag perdido!", "A criança pode estar longe. Abra o FindLK.")
        }
    }

    private fun scheduleDisconnectAlarm() {
        handler.removeCallbacks(disconnectAlarm)
        handler.postDelayed(disconnectAlarm, TagProtocol.DISCONNECT_ALARM_MS)
    }

    private fun sendConfig() {
        val phone = prefs.phone
        write(TagProtocol.CONFIG, "t=$phone")
    }

    private fun onTagState(state: Int) {
        TagRepository.update { it.copy(tagState = state) }
        when (state) {
            TagProtocol.STATE_LOST ->
                Alarm.start(this, "Criança perdida!", "A tag entrou em PERDIDO. Abra o FindLK.")
            TagProtocol.STATE_DISARMED, TagProtocol.STATE_ARMED ->
                if (TagRepository.state.value.alarm == "Criança perdida!") Alarm.stop(this)
        }
        updateForeground()
    }

    private fun updateForeground() {
        val s = TagRepository.state.value
        val text = if (s.connected) "Tag ${TagProtocol.stateName(s.tagState).lowercase()}" else "Procurando a tag…"
        getSystemService(android.app.NotificationManager::class.java)
            .notify(Alarm.ID_SERVICE, foregroundNotification(text))
    }

    /* ---------- Callbacks do Bluetooth ---------- */

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            handler.post {
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    handler.removeCallbacks(disconnectAlarm)
                    if (TagRepository.state.value.alarm == "Sinal da tag perdido!") Alarm.stop(this@TagService)
                    authenticated = false
                    configSent = false
                    TagRepository.update { it.copy(connected = true, message = "Conectando com segurança…") }
                    g.discoverServices()
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    ready = false
                    busy = false
                    queue.clear()
                    smoothedRssi = null
                    TagRepository.update { it.copy(connected = false, level = null, rssi = null, message = "Sem conexão com a tag") }
                    if (prefs.protecting) scheduleDisconnectAlarm()
                }
                updateForeground()
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            handler.post {
                if (g.getService(TagProtocol.SERVICE) == null) {
                    TagRepository.update { it.copy(message = "Serviço FindLK não encontrado na tag") }
                    return@post
                }
                ready = true
                // Assina o Estado, manda a configuração e o primeiro heartbeat
                enqueue { gg ->
                    val chr = gg.getService(TagProtocol.SERVICE).getCharacteristic(TagProtocol.STATE)
                    gg.setCharacteristicNotification(chr, true)
                    val cccd = chr.getDescriptor(TagProtocol.CCCD) ?: return@enqueue false
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        gg.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothStatusCodes.SUCCESS
                    } else {
                        @Suppress("DEPRECATION")
                        cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                        @Suppress("DEPRECATION")
                        gg.writeDescriptor(cccd)
                    }
                }
                write(TagProtocol.HEARTBEAT, prefs.name.ifBlank { "Responsável" })
                enqueue { gg ->
                    val chr = gg.getService(TagProtocol.SERVICE).getCharacteristic(TagProtocol.STATE)
                    gg.readCharacteristic(chr)
                }
                next()
            }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            val ok = status == BluetoothGatt.GATT_SUCCESS
            handler.post {
                when {
                    !ok -> TagRepository.update { it.copy(message = "A tag recusou a escrita (código $status)") }
                    c.uuid == TagProtocol.CONFIG -> configSent = true
                    c.uuid == TagProtocol.HEARTBEAT && !authenticated -> {
                        authenticated = true
                        TagRepository.update { it.copy(message = "Conectado") }
                        if (!configSent && prefs.phone.isNotBlank()) sendConfig()
                        enqueue { gg -> gg.readCharacteristic(gg.getService(TagProtocol.SERVICE).getCharacteristic(TagProtocol.STATE)) }
                    }
                }
            }
            done()
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) = done()

        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS && c.uuid == TagProtocol.STATE && value.isNotEmpty()) {
                handler.post { onTagState(value[0].toInt()) }
            }
            done()
        }

        @Deprecated("Android 12 ou anterior")
        override fun onCharacteristicRead(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            @Suppress("DEPRECATION")
            val value = c.value ?: ByteArray(0)
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                if (status == BluetoothGatt.GATT_SUCCESS && c.uuid == TagProtocol.STATE && value.isNotEmpty()) {
                    handler.post { onTagState(value[0].toInt()) }
                }
                done()
            }
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            if (c.uuid == TagProtocol.STATE && value.isNotEmpty()) handler.post { onTagState(value[0].toInt()) }
        }

        @Deprecated("Android 12 ou anterior")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                @Suppress("DEPRECATION")
                val value = c.value ?: return
                if (c.uuid == TagProtocol.STATE && value.isNotEmpty()) handler.post { onTagState(value[0].toInt()) }
            }
        }

        override fun onReadRemoteRssi(g: BluetoothGatt, rssi: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                handler.post {
                    // Média móvel, como na tag, para a barra não ficar pulando
                    val s = smoothedRssi?.let { it + 0.3 * (rssi - it) } ?: rssi.toDouble()
                    smoothedRssi = s
                    TagRepository.update { it.copy(rssi = rssi, level = TagProtocol.levelForRssi(s)) }
                }
            }
            done()
        }
    }
}
