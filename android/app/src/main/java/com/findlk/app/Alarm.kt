package com.findlk.app

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.Ringtone
import android.media.RingtoneManager
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import androidx.core.app.NotificationCompat

/** Notificações e alarme sonoro (toca em loop até o responsável parar). */
object Alarm {
    const val CHANNEL_SERVICE = "protegendo"
    const val CHANNEL_ALARM = "alarme"
    const val ID_SERVICE = 1
    private const val ID_ALARM = 2

    private var ringtone: Ringtone? = null

    fun createChannels(context: Context) {
        val nm = context.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL_SERVICE, "Proteção ativa", NotificationManager.IMPORTANCE_LOW).apply {
                description = "Mostra que o FindLK está conectado à tag"
            }
        )
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL_ALARM, "Alarme de criança perdida", NotificationManager.IMPORTANCE_HIGH).apply {
                description = "Toca quando a tag perde o contato ou entra em PERDIDO"
                enableVibration(true)
                setBypassDnd(true)
            }
        )
    }

    fun openAppIntent(context: Context): PendingIntent = PendingIntent.getActivity(
        context, 0,
        Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
        PendingIntent.FLAG_IMMUTABLE,
    )

    fun start(context: Context, title: String, text: String) {
        TagRepository.update { it.copy(alarm = title) }

        val stop = PendingIntent.getService(
            context, 1,
            Intent(context, TagService::class.java).setAction(TagService.ACTION_STOP_ALARM),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val notification = NotificationCompat.Builder(context, CHANNEL_ALARM)
            .setSmallIcon(R.drawable.ic_stat)
            .setContentTitle(title)
            .setContentText(text)
            .setPriority(NotificationCompat.PRIORITY_MAX)
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setContentIntent(openAppIntent(context))
            .setFullScreenIntent(openAppIntent(context), true)
            .addAction(0, "Parar alarme", stop)
            .setOngoing(true)
            .build()
        context.getSystemService(NotificationManager::class.java).notify(ID_ALARM, notification)

        if (ringtone?.isPlaying != true) {
            val uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
                ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)
            ringtone = RingtoneManager.getRingtone(context, uri)?.apply {
                audioAttributes = AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ALARM)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build()
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) isLooping = true
                play()
            }
        }
        vibrator(context).vibrate(VibrationEffect.createWaveform(longArrayOf(0, 800, 400), 0))
    }

    fun stop(context: Context) {
        ringtone?.stop()
        ringtone = null
        vibrator(context).cancel()
        context.getSystemService(NotificationManager::class.java).cancel(ID_ALARM)
        TagRepository.update { it.copy(alarm = null) }
    }

    private fun vibrator(context: Context): Vibrator =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            context.getSystemService(VibratorManager::class.java).defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            context.getSystemService(Vibrator::class.java)
        }
}
