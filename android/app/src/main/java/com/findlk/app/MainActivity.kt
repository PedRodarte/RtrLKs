package com.findlk.app

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.os.PowerManager
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle

/* Mesmas cores da página do QR (docs/estilo.css) */
private val Bg = Color(0xFF14171D)
private val Card = Color(0xFF1D2129)
private val Fg = Color(0xFFEEF0F4)
private val Muted = Color(0xFFA3A9B6)
private val Accent = Color(0xFF34B37A)
private val AccentFg = Color(0xFF08140E)
private val Urgent = Color(0xFFA1231A)
private val Line = Color(0xFF2C313B)

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Alarm.createChannels(this)
        val prefs = Prefs(this)
        if (prefs.protecting && prefs.tagAddress.isNotBlank() && hasPermissions(this)) {
            TagService.send(this, TagService.ACTION_START)
        }
        setContent {
            MaterialTheme(colorScheme = darkColorScheme(primary = Accent, background = Bg, surface = Card)) {
                Box(Modifier.fillMaxSize().background(Bg)) { App(prefs) }
            }
        }
    }
}

private fun requiredPermissions(): Array<String> = buildList {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        add(Manifest.permission.BLUETOOTH_SCAN)
        add(Manifest.permission.BLUETOOTH_CONNECT)
    } else {
        add(Manifest.permission.ACCESS_COARSE_LOCATION)
        add(Manifest.permission.ACCESS_FINE_LOCATION)
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) add(Manifest.permission.POST_NOTIFICATIONS)
}.toTypedArray()

private fun hasPermissions(context: Context) = requiredPermissions().all {
    ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
}

/* ---------- Componentes visuais ---------- */

@Composable
private fun Title(text: String) = Text(text, color = Fg, fontSize = 26.sp, fontWeight = FontWeight.Bold, lineHeight = 31.sp)

@Composable
private fun Support(text: String, color: Color = Muted) = Text(text, color = color, fontSize = 17.sp, lineHeight = 24.sp)

@Composable
private fun Body(text: String, color: Color = Fg) = Text(text, color = color, fontSize = 16.sp, lineHeight = 23.sp)

@Composable
private fun Box12(content: @Composable () -> Unit) = Column(
    Modifier.fillMaxWidth().background(Card, RoundedCornerShape(10.dp)).border(1.dp, Line, RoundedCornerShape(10.dp))
        .padding(12.dp),
    verticalArrangement = Arrangement.spacedBy(12.dp),
) { content() }

@Composable
private fun Cta(text: String, onClick: () -> Unit, enabled: Boolean = true) = Button(
    onClick = onClick, enabled = enabled,
    modifier = Modifier.fillMaxWidth().heightIn(min = 56.dp),
    shape = RoundedCornerShape(20.dp),
    colors = ButtonDefaults.buttonColors(containerColor = Accent, contentColor = AccentFg),
) { Text(text, fontSize = 18.sp, fontWeight = FontWeight.Bold) }

@Composable
private fun CtaOutline(text: String, onClick: () -> Unit) = OutlinedButton(
    onClick = onClick,
    modifier = Modifier.fillMaxWidth().heightIn(min = 52.dp),
    shape = RoundedCornerShape(20.dp),
    border = androidx.compose.foundation.BorderStroke(2.dp, Accent),
) { Text(text, fontSize = 18.sp, fontWeight = FontWeight.Bold, color = Accent) }

/* ---------- Telas ---------- */

@Composable
private fun App(prefs: Prefs) {
    val context = LocalContext.current
    var permitted by remember { mutableStateOf(hasPermissions(context)) }
    var paired by remember { mutableStateOf(prefs.tagAddress.isNotBlank()) }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        permitted = hasPermissions(context)
    }

    Column(
        Modifier.fillMaxSize().systemBarsPadding().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Title("FindLK")
        when {
            !permitted -> {
                Support("Para encontrar a tag e avisar você, o FindLK precisa de acesso ao Bluetooth e às notificações.")
                Cta("Permitir", { launcher.launch(requiredPermissions()) })
            }
            !paired -> SetupScreen(prefs) { paired = true }
            else -> MainScreen(prefs) { paired = false }
        }
        Spacer(Modifier.height(8.dp))
        Body("Versão ${BuildConfigVersion.name}", Muted)
    }
}

private object BuildConfigVersion {
    const val name = "01"
}

@SuppressLint("MissingPermission")
@Composable
private fun SetupScreen(prefs: Prefs, onPaired: () -> Unit) {
    val context = LocalContext.current
    var name by remember { mutableStateOf(prefs.name) }
    var phone by remember { mutableStateOf(prefs.phone) }
    var scanning by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf("") }
    val found = remember { mutableStateListOf<BluetoothDevice>() }
    val adapter = remember { context.getSystemService(BluetoothManager::class.java).adapter }

    val scanCallback = remember {
        object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                if (found.none { it.address == result.device.address }) found.add(result.device)
            }
        }
    }

    fun stopScan() {
        if (scanning) adapter.bluetoothLeScanner?.stopScan(scanCallback)
        scanning = false
    }

    // Recebe o fim do pareamento (o Android pede o código que aparece na tela da tag)
    DisposableEffect(Unit) {
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, i: Intent) {
                @Suppress("DEPRECATION")
                val device: BluetoothDevice = i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE) ?: return
                when (i.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1)) {
                    BluetoothDevice.BOND_BONDED -> {
                        prefs.tagAddress = device.address
                        prefs.tagName = device.name ?: "FindLK"
                        TagService.send(context, TagService.ACTION_START)
                        onPaired()
                    }
                    BluetoothDevice.BOND_NONE -> status = "O pareamento não foi concluído. Tente de novo."
                }
            }
        }
        ContextCompat.registerReceiver(
            context, receiver, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED), ContextCompat.RECEIVER_EXPORTED,
        )
        onDispose {
            context.unregisterReceiver(receiver)
            stopScan()
        }
    }

    Support("Configure uma vez: seu nome aparece na tag e o WhatsApp vai no QR code para quem encontrar a criança.")
    Box12 {
        OutlinedTextField(name, { name = it }, label = { Text("Seu nome (ex.: Mãe, Pai)") }, singleLine = true,
            modifier = Modifier.fillMaxWidth())
        OutlinedTextField(phone, { phone = it.filter(Char::isDigit) }, label = { Text("WhatsApp com 55 + DDD") },
            singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
            modifier = Modifier.fillMaxWidth())
    }

    if (adapter == null || !adapter.isEnabled) {
        Body("Ligue o Bluetooth do celular para procurar a tag.", Muted)
        return
    }

    Cta(if (scanning) "Procurando…" else "Procurar a tag", enabled = !scanning, onClick = {
        prefs.name = name
        prefs.phone = phone
        found.clear()
        status = ""
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(TagProtocol.SERVICE)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        adapter.bluetoothLeScanner?.startScan(listOf(filter), settings, scanCallback)
        scanning = true
        Handler(Looper.getMainLooper()).postDelayed({ stopScan() }, 15_000)
    })

    found.forEach { device ->
        Box12 {
            Body(device.name ?: "Tag FindLK")
            CtaOutline("Parear com esta tag") {
                stopScan()
                prefs.name = name
                prefs.phone = phone
                if (device.bondState == BluetoothDevice.BOND_BONDED) {
                    prefs.tagAddress = device.address
                    prefs.tagName = device.name ?: "FindLK"
                    TagService.send(context, TagService.ACTION_START)
                    onPaired()
                } else {
                    status = "Digite no celular o código de 6 dígitos que aparece na tela da tag."
                    device.createBond()
                }
            }
        }
    }
    if (scanning && found.isEmpty()) Body("Aproxime o celular da tag ligada.", Muted)
    if (status.isNotBlank()) Body(status, Muted)
}

@Composable
private fun MainScreen(prefs: Prefs, onForget: () -> Unit) {
    val context = LocalContext.current
    val s by TagRepository.state.collectAsStateWithLifecycle()
    var editing by remember { mutableStateOf(false) }

    if (s.alarm != null) {
        Column(
            Modifier.fillMaxWidth().background(Urgent, RoundedCornerShape(10.dp)).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text("🚨 ${s.alarm}", color = Color.White, fontSize = 24.sp, fontWeight = FontWeight.Bold)
            Support("Vá até o último lugar onde a criança estava. Quando encontrar, toque em Encontrei.", Color.White)
            Cta("Parar alarme", { TagService.send(context, TagService.ACTION_STOP_ALARM) })
        }
    }

    Box12 {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Body(prefs.tagName.ifBlank { "Tag" })
            Spacer(Modifier.weight(1f))
            Body(
                when {
                    !s.protecting -> "Proteção desligada"
                    s.connected -> "Conectada"
                    else -> "Sem conexão"
                },
                if (s.connected) Accent else Muted,
            )
        }
        Support("Estado: ${TagProtocol.stateName(s.tagState)}", if (s.tagState == TagProtocol.STATE_LOST) Color(0xFFFB923C) else Fg)
        ProximityBar(s.level)
        if (s.message.isNotBlank()) Body(s.message, Muted)
    }

    if (s.protecting) {
        if (s.tagState == TagProtocol.STATE_LOST || s.tagState == TagProtocol.STATE_COMING || s.alarm != null) {
            Cta("Estou chegando", { TagService.send(context, TagService.ACTION_COMING) })
            Cta("Encontrei a criança", { TagService.send(context, TagService.ACTION_FOUND) })
        }
        CtaOutline("Desarmar (estou com a criança)") { TagService.send(context, TagService.ACTION_DISARM) }
        CtaOutline("Desligar a proteção") { TagService.send(context, TagService.ACTION_STOP) }
    } else {
        Cta("Ligar a proteção", { TagService.send(context, TagService.ACTION_START) })
    }

    BatteryHint()

    if (!editing) {
        CtaOutline("Meu nome e WhatsApp") { editing = true }
    } else {
        var name by remember { mutableStateOf(prefs.name) }
        var phone by remember { mutableStateOf(prefs.phone) }
        Box12 {
            OutlinedTextField(name, { name = it }, label = { Text("Seu nome") }, singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(phone, { phone = it.filter(Char::isDigit) }, label = { Text("WhatsApp com 55 + DDD") },
                singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
                modifier = Modifier.fillMaxWidth())
            Cta("Salvar e enviar para a tag", {
                prefs.name = name
                prefs.phone = phone
                TagService.send(context, TagService.ACTION_SEND_CONFIG)
                editing = false
            })
            CtaOutline("Esquecer esta tag") {
                TagService.send(context, TagService.ACTION_STOP)
                prefs.tagAddress = ""
                prefs.tagName = ""
                onForget()
            }
        }
    }
}

@Composable
private fun ProximityBar(level: Int?) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.Bottom) {
            for (i in 0 until 5) {
                Box(
                    Modifier.width(22.dp).height((10 + i * 8).dp).background(
                        if (level != null && i <= level) Accent else Line, RoundedCornerShape(4.dp),
                    )
                )
            }
        }
        Body(if (level == null) "Proximidade: —" else "Proximidade: ${TagProtocol.LEVEL_NAMES[level]}", Muted)
    }
}

@SuppressLint("BatteryLife")
@Composable
private fun BatteryHint() {
    val context = LocalContext.current
    val pm = context.getSystemService(PowerManager::class.java)
    if (pm.isIgnoringBatteryOptimizations(context.packageName)) return
    Box12 {
        Body("Para o alarme funcionar com a tela apagada, libere o FindLK da economia de bateria.", Muted)
        CtaOutline("Liberar em segundo plano") {
            context.startActivity(
                Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:${context.packageName}"))
            )
        }
    }
}
