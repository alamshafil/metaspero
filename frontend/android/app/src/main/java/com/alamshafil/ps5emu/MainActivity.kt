// SPDX-License-Identifier: GPL-2.0-or-later
package com.alamshafil.ps5emu

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.core.view.WindowCompat
import metaspero.ui.ContentView
import metaspero.ui.KytyBridge
import org.libsdl.app.SDL

private val DarkColorScheme = darkColorScheme(
    primary = Color(0xFF2979FF),
    onPrimary = Color.White,
    primaryContainer = Color(0xFF1E3A8A),
    onPrimaryContainer = Color(0xFFDBEAFE),
    secondary = Color(0xFF60A5FA),
    onSecondary = Color(0xFF0F172A),
    background = Color(0xFF080B10),
    onBackground = Color(0xFFF1F5F9),
    surface = Color(0xFF0F141C),
    onSurface = Color(0xFFF1F5F9),
    surfaceVariant = Color(0xFF161F2E),
    onSurfaceVariant = Color(0xFF94A3B8),
    outline = Color(0xFF334155)
)

class MainActivity : ComponentActivity() {

    private var installedDrivers: List<DriverInfo> = emptyList()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Initialize SDL3 Android host context
        SDL.setContext(this)
        SDL.setupJNI()

        // Initialize SkipFoundation process runtime context
        skip.foundation.ProcessInfo.launch(applicationContext)

        // Initialize native bridge directories
        NativeBridge.nativeInit(filesDir.absolutePath, cacheDir.absolutePath)

        checkStoragePermission()
        refreshDrivers()

        // App sandbox documents directory
        KytyBridge.addCustomGamePath(filesDir.absolutePath)

        // Bridge SwiftUI launch action into native Android EmulationActivity
        KytyBridge.onLaunchGame = { gamePath ->
            val driverId = GpuDriverManager.getSelectedDriverId(this@MainActivity)
            val driver = installedDrivers.firstOrNull { it.id == driverId } ?: installedDrivers.firstOrNull()
            if (driver != null) {
                GpuDriverManager.applyDriver(this@MainActivity, driver)
            }

            val prefs = getSharedPreferences("defaults", Context.MODE_PRIVATE)
            val res = prefs.getString("resolutionScale", "720p") ?: "720p"
            val present = prefs.getString("presentMode", "Mailbox") ?: "Mailbox"

            val intent = Intent(this@MainActivity, EmulationActivity::class.java).apply {
                putExtra("EXTRA_GAME_PATH", gamePath)
                putExtra("EXTRA_RESOLUTION", res)
                putExtra("EXTRA_PRESENT_MODE", present)
            }
            startActivity(intent)
        }

        // Bridge '+' Import action into Android Storage Access Framework
        KytyBridge.onPickFolderRequested = {
            runOnUiThread {
                val options = arrayOf("Select Game Folder (Scan for games)", "Select Single Game / ELF File")
                com.google.android.material.dialog.MaterialAlertDialogBuilder(this@MainActivity)
                    .setTitle("Import Games")
                    .setItems(options) { _, which ->
                        if (which == 0) {
                            try {
                                val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).apply {
                                    addFlags(
                                        Intent.FLAG_GRANT_READ_URI_PERMISSION or
                                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                                    )
                                }
                                startActivityForResult(intent, REQUEST_CODE_PICK_FOLDER)
                            } catch (e: Exception) {
                                Toast.makeText(this@MainActivity, "Folder picker error: ${e.message}", Toast.LENGTH_SHORT).show()
                            }
                        } else {
                            try {
                                val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                                    addCategory(Intent.CATEGORY_OPENABLE)
                                    type = "*/*"
                                    addFlags(
                                        Intent.FLAG_GRANT_READ_URI_PERMISSION or
                                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                                    )
                                }
                                startActivityForResult(intent, REQUEST_CODE_PICK_FILE)
                            } catch (e: Exception) {
                                Toast.makeText(this@MainActivity, "File picker error: ${e.message}", Toast.LENGTH_SHORT).show()
                            }
                        }
                    }
                    .setNegativeButton("Cancel", null)
                    .show()
            }
        }

        // Bridge GPU driver actions from SwiftUI SettingsView
        KytyBridge.onInstallDriverRequested = {
            runOnUiThread {
                try {
                    val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                        addCategory(Intent.CATEGORY_OPENABLE)
                        type = "application/zip"
                        putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/zip", "application/octet-stream", "*/*"))
                    }
                    startActivityForResult(intent, REQUEST_CODE_PICK_DRIVER)
                } catch (e: Exception) {
                    Toast.makeText(this@MainActivity, "Driver picker error: ${e.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }

        KytyBridge.onSelectDriverRequested = { driverId ->
            runOnUiThread {
                val driver = installedDrivers.find { it.id == driverId } ?: GpuDriverManager.DEFAULT_DRIVER
                GpuDriverManager.applyDriver(this@MainActivity, driver)
                Toast.makeText(this@MainActivity, "GPU Driver: ${driver.name}", Toast.LENGTH_SHORT).show()
                refreshDrivers(driver.id)
            }
        }

        WindowCompat.getInsetsController(window, window.decorView).apply {
            isAppearanceLightStatusBars = false
            isAppearanceLightNavigationBars = false
        }

        setContent {
            val colorScheme = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                dynamicDarkColorScheme(this@MainActivity)
            } else {
                DarkColorScheme
            }

            MaterialTheme(colorScheme = colorScheme) {
                Surface(
                    modifier = Modifier
                        .fillMaxSize()
                        .statusBarsPadding()
                        .navigationBarsPadding(),
                    color = colorScheme.background
                ) {
                    ContentView().Compose()
                }
            }
        }
    }

    private fun refreshDrivers(selectDriverId: String? = null) {
        installedDrivers = GpuDriverManager.getInstalledDrivers(this)
        val targetId = selectDriverId ?: GpuDriverManager.getSelectedDriverId(this)
        val index = installedDrivers.indexOfFirst { it.id == targetId }
        val activeDriver = if (index >= 0) installedDrivers[index] else GpuDriverManager.DEFAULT_DRIVER
        GpuDriverManager.applyDriver(this, activeDriver)

        // Sync drivers into KytyBridge for SettingsView
        KytyBridge.clearInstalledDrivers()
        for (driver in installedDrivers) {
            KytyBridge.addInstalledDriver(driver.id, driver.name, driver.isSystemDefault)
        }
        KytyBridge.selectedDriverId = activeDriver.id
    }

    private fun checkStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).apply {
                        data = Uri.parse("package:$packageName")
                    }
                    startActivity(intent)
                } catch (_: Exception) {
                    val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    startActivity(intent)
                }
            }
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != RESULT_OK || data == null) return

        when (requestCode) {
            REQUEST_CODE_PICK_FOLDER -> {
                val uri = data.data
                if (uri != null) {
                    try {
                        contentResolver.takePersistableUriPermission(
                            uri,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION
                        )
                    } catch (_: Exception) {}
                    val rawPath = uri.path ?: uri.toString()
                    val posixPath = if (rawPath.contains("primary:")) {
                        "/storage/emulated/0/" + rawPath.substringAfter("primary:").trimStart('/')
                    } else if (rawPath.contains("tree/")) {
                        val sub = rawPath.substringAfter("tree/").substringAfter(":")
                        "/storage/emulated/0/" + sub.trimStart('/')
                    } else {
                        rawPath
                    }
                    KytyBridge.addCustomGamePath(posixPath)
                    Toast.makeText(this, "Added: $posixPath", Toast.LENGTH_SHORT).show()
                }
            }
            REQUEST_CODE_PICK_FILE -> {
                val uri = data.data
                if (uri != null) {
                    try {
                        contentResolver.takePersistableUriPermission(
                            uri,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION
                        )
                    } catch (_: Exception) {}
                    val rawPath = uri.path ?: uri.toString()
                    val posixPath = if (rawPath.contains("primary:")) {
                        "/storage/emulated/0/" + rawPath.substringAfter("primary:").trimStart('/')
                    } else if (rawPath.contains("document/")) {
                        val sub = rawPath.substringAfter("document/").substringAfter(":")
                        "/storage/emulated/0/" + sub.trimStart('/')
                    } else {
                        rawPath
                    }
                    val parentDir = java.io.File(posixPath).parent ?: posixPath
                    KytyBridge.addCustomGamePath(parentDir)
                    Toast.makeText(this, "Added: $posixPath", Toast.LENGTH_SHORT).show()
                }
            }
            REQUEST_CODE_PICK_DRIVER -> {
                val uri = data.data
                if (uri != null) {
                    Toast.makeText(this, "Installing GPU driver...", Toast.LENGTH_SHORT).show()
                    Thread {
                        val installed = GpuDriverManager.installDriverZip(this, uri)
                        runOnUiThread {
                            if (installed != null) {
                                Toast.makeText(this, "Installed: ${installed.name}", Toast.LENGTH_LONG).show()
                                refreshDrivers(installed.id)
                            } else {
                                Toast.makeText(this, "Failed to install driver ZIP", Toast.LENGTH_LONG).show()
                            }
                        }
                    }.start()
                }
            }
        }
    }

    companion object {
        private const val REQUEST_CODE_PICK_FOLDER = 1001
        private const val REQUEST_CODE_PICK_DRIVER = 1002
        private const val REQUEST_CODE_PICK_FILE = 1003
    }
}
