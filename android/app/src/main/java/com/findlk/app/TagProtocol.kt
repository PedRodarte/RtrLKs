package com.findlk.app

import java.util.UUID

/** Protocolo GATT da tag FindLK (igual ao firmware em main/tag_ble.c). */
object TagProtocol {
    private fun uuid(n: Int): UUID = UUID.fromString("a7c3%04x-1b8e-4f2a-9d6e-3c5b7e9f0a12".format(n))

    val SERVICE: UUID = uuid(0x01)
    val HEARTBEAT: UUID = uuid(0x02)
    val COMING: UUID = uuid(0x03)
    val COMMAND: UUID = uuid(0x04)
    val STATE: UUID = uuid(0x05)
    val CONFIG: UUID = uuid(0x06)
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    const val STATE_DISARMED = 0
    const val STATE_ARMED = 1
    const val STATE_LOST = 2
    const val STATE_COMING = 3

    const val CMD_FOUND = "ENCONTRADO"
    const val CMD_DISARM = "DESARMAR"

    const val HEARTBEAT_PERIOD_MS = 5_000L

    /** Sem conexão por este tempo durante a proteção, o próprio app dispara o alarme. */
    const val DISCONNECT_ALARM_MS = 20_000L

    /** Mesmos limites de RSSI do firmware (aproximando, próximo, muito perto, sinal máximo). */
    private val RSSI_LEVELS = intArrayOf(-85, -75, -65, -55)

    val LEVEL_NAMES = listOf("Longe", "Aproximando", "Próximo", "Muito perto", "Sinal máximo")

    fun stateName(state: Int?): String = when (state) {
        STATE_DISARMED -> "Desarmado"
        STATE_ARMED -> "Armado"
        STATE_LOST -> "PERDIDO"
        STATE_COMING -> "Chegando"
        else -> "—"
    }

    fun levelForRssi(rssi: Double): Int = RSSI_LEVELS.count { rssi >= it }
}
