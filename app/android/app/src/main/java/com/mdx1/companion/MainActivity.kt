package com.mdx1.companion

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.ui.graphics.Color
import com.mdx1.companion.link.DetectorSession
import com.mdx1.companion.ui.MainScreen

class MainActivity : ComponentActivity() {
    private val session: DetectorSession by viewModels()

    private val permissions = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
        if (granted.values.all { it } && session.autoConnect) session.startScan()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            val copper = Color(0xFFB86B33)
            val scheme = if (isSystemInDarkTheme()) darkColorScheme(primary = copper, secondary = Color(0xFF2A855C), tertiary = Color(0xFF9E3329))
            else lightColorScheme(primary = copper, secondary = Color(0xFF2A855C), tertiary = Color(0xFF9E3329))
            MaterialTheme(colorScheme = scheme) { MainScreen(session, onNeedPermissions = ::requestBle) }
        }
        requestBle()
    }

    private fun requestBle() {
        val needed = if (Build.VERSION.SDK_INT >= 31) arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        permissions.launch(needed)
    }
}
