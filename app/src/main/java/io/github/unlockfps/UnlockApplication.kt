package io.github.unlockfps

import android.app.Application
import com.topjohnwu.superuser.Shell

class UnlockApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        Shell.setDefaultBuilder(
            Shell.Builder.create()
                .setFlags(Shell.FLAG_MOUNT_MASTER)
                .setTimeout(10)
        )
    }
}
