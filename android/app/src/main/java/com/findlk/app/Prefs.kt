package com.findlk.app

import android.content.Context

/** Configuração do responsável, guardada no celular. */
class Prefs(context: Context) {
    private val sp = context.getSharedPreferences("findlk", Context.MODE_PRIVATE)

    /** Nome enviado no heartbeat e mostrado na tag ("Mãe está chegando"). */
    var name: String
        get() = sp.getString("name", "") ?: ""
        set(v) = sp.edit().putString("name", v.trim()).apply()

    /** WhatsApp para o QR code da tag: só dígitos, com 55 + DDD. */
    var phone: String
        get() = sp.getString("phone", "") ?: ""
        set(v) = sp.edit().putString("phone", v.filter(Char::isDigit)).apply()

    /** Endereço da tag pareada; vazio se nenhuma. */
    var tagAddress: String
        get() = sp.getString("tag", "") ?: ""
        set(v) = sp.edit().putString("tag", v).apply()

    var tagName: String
        get() = sp.getString("tag_name", "") ?: ""
        set(v) = sp.edit().putString("tag_name", v).apply()

    var protecting: Boolean
        get() = sp.getBoolean("protecting", false)
        set(v) = sp.edit().putBoolean("protecting", v).apply()
}
