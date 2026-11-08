// SPDX-License-Identifier: GPL-2.0-or-later
package com.alamshafil.ps5emu

import android.content.Context
import android.net.Uri
import android.util.Log
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.util.zip.ZipInputStream

data class DriverInfo(
    val id: String,
    val name: String,
    val driverDir: String? = null,
    val driverLibrary: String? = null
) {
    val isSystemDefault: Boolean
        get() = driverDir == null || driverLibrary == null

    override fun toString(): String = name
}

object GpuDriverManager {

    private const val TAG = "GpuDriverManager"
    private const val PREFS_NAME = "gpu_driver_prefs"
    private const val KEY_SELECTED_DRIVER = "selected_driver_id"

    val DEFAULT_DRIVER = DriverInfo(
        id = "system_default",
        name = "System Default Vulkan",
        driverDir = null,
        driverLibrary = null
    )

    private fun getDriversRoot(context: Context): File {
        val root = File(context.filesDir, "gpu_drivers")
        if (!root.exists()) {
            root.mkdirs()
        }
        return root
    }

    fun getInstalledDrivers(context: Context): List<DriverInfo> {
        val list = mutableListOf(DEFAULT_DRIVER)
        val root = getDriversRoot(context)
        val dirs = root.listFiles { f -> f.isDirectory } ?: return list

        for (dir in dirs) {
            val metaFile = File(dir, "meta.json")
            var displayName = dir.name
            var libName: String? = null

            if (metaFile.exists()) {
                try {
                    val json = JSONObject(metaFile.readText())
                    if (json.has("name")) {
                        displayName = json.getString("name")
                    }
                    if (json.has("libraryName")) {
                        libName = json.getString("libraryName")
                    } else if (json.has("driverLibrary")) {
                        libName = json.getString("driverLibrary")
                    }
                } catch (e: Exception) {
                    Log.w(TAG, "Failed to parse meta.json in ${dir.name}: ${e.message}")
                }
            }

            if (libName == null) {
                val candidateLibs = listOf(
                    "vulkan.adreno.so",
                    "vulkan.turnip.so",
                    "libvulkan_freedreno.so",
                    "libvulkan.so"
                )
                for (c in candidateLibs) {
                    if (File(dir, c).exists()) {
                        libName = c
                        break
                    }
                }
            }

            if (libName == null) {
                val soFiles = dir.listFiles { f -> f.isFile && f.name.endsWith(".so") }
                if (!soFiles.isNullOrEmpty()) {
                    libName = soFiles[0].name
                }
            }

            list.add(
                DriverInfo(
                    id = dir.name,
                    name = displayName,
                    driverDir = dir.absolutePath,
                    driverLibrary = libName
                )
            )
        }
        return list
    }

    fun installDriverZip(context: Context, zipUri: Uri): DriverInfo? {
        val contentResolver = context.contentResolver
        val inputStream: InputStream = contentResolver.openInputStream(zipUri) ?: run {
            Log.e(TAG, "Could not open URI: $zipUri")
            return null
        }

        val tempDir = File(context.cacheDir, "driver_install_temp_${System.currentTimeMillis()}")
        tempDir.mkdirs()

        try {
            ZipInputStream(inputStream).use { zis ->
                var entry = zis.nextEntry
                val buffer = ByteArray(8192)
                while (entry != null) {
                    val entryName = entry.name.replace('\\', '/')
                    if (entryName.contains("..")) {
                        entry = zis.nextEntry
                        continue
                    }

                    val outFile = File(tempDir, entryName)
                    if (entry.isDirectory) {
                        outFile.mkdirs()
                    } else {
                        outFile.parentFile?.mkdirs()
                        FileOutputStream(outFile).use { fos ->
                            var read: Int
                            while (zis.read(buffer).also { read = it } != -1) {
                                fos.write(buffer, 0, read)
                            }
                        }
                    }
                    zis.closeEntry()
                    entry = zis.nextEntry
                }
            }

            var payloadDir: File = tempDir
            val metaInRoot = File(tempDir, "meta.json")
            if (!metaInRoot.exists()) {
                val subDirs = tempDir.listFiles { f -> f.isDirectory }
                if (subDirs != null && subDirs.size == 1 && File(subDirs[0], "meta.json").exists()) {
                    payloadDir = subDirs[0]
                }
            }

            var driverName = "Custom Driver"
            var libName: String? = null
            val metaFile = File(payloadDir, "meta.json")
            if (metaFile.exists()) {
                try {
                    val json = JSONObject(metaFile.readText())
                    if (json.has("name")) driverName = json.getString("name")
                    if (json.has("libraryName")) libName = json.getString("libraryName")
                } catch (e: Exception) {
                    Log.w(TAG, "Failed to read extracted meta.json: ${e.message}")
                }
            }

            val sanitizedId = driverName.lowercase()
                .replace(Regex("[^a-z0-9_]+"), "_")
                .trim('_')
                .ifEmpty { "driver_${System.currentTimeMillis()}" }

            val targetDir = File(getDriversRoot(context), sanitizedId)
            if (targetDir.exists()) {
                targetDir.deleteRecursively()
            }
            targetDir.mkdirs()

            payloadDir.copyRecursively(targetDir, overwrite = true)

            targetDir.walkTopDown().forEach { file ->
                if (file.isFile && file.name.endsWith(".so")) {
                    file.setReadable(true, false)
                    file.setExecutable(true, false)
                }
            }

            if (libName == null) {
                val candidateLibs = listOf(
                    "vulkan.adreno.so",
                    "vulkan.turnip.so",
                    "libvulkan_freedreno.so",
                    "libvulkan.so"
                )
                for (c in candidateLibs) {
                    if (File(targetDir, c).exists()) {
                        libName = c
                        break
                    }
                }
            }

            return DriverInfo(
                id = sanitizedId,
                name = driverName,
                driverDir = targetDir.absolutePath,
                driverLibrary = libName
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to install driver ZIP: ${e.message}", e)
            return null
        } finally {
            tempDir.deleteRecursively()
        }
    }

    fun getSelectedDriverId(context: Context): String {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getString(KEY_SELECTED_DRIVER, DEFAULT_DRIVER.id) ?: DEFAULT_DRIVER.id
    }

    fun saveSelectedDriverId(context: Context, driverId: String) {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().putString(KEY_SELECTED_DRIVER, driverId).apply()
    }

    private fun ensurePlatformDependencies(dir: File) {
        val needed = listOf("/system/lib64/libhardware.so", "/system/lib64/libsync.so")
        for (srcPath in needed) {
            val src = File(srcPath)
            if (src.exists() && src.canRead()) {
                val dst = File(dir, src.name)
                if (!dst.exists()) {
                    try {
                        src.copyTo(dst, overwrite = true)
                    } catch (e: Exception) {
                        Log.w(TAG, "Could not copy $srcPath: ${e.message}")
                    }
                }
            }
        }
    }

    fun applyDriver(context: Context, driver: DriverInfo): Boolean {
        saveSelectedDriverId(context, driver.id)
        val hookDir = context.applicationInfo.nativeLibraryDir
        return if (driver.isSystemDefault) {
            Log.i(TAG, "Applying System Default Vulkan driver")
            NativeBridge.nativeSetCustomDriver(null, null, hookDir)
        } else {
            Log.i(TAG, "Applying Custom driver: ${driver.name} (${driver.driverLibrary})")
            if (driver.driverDir != null) {
                val dirFile = File(driver.driverDir)
                ensurePlatformDependencies(dirFile)
                dirFile.listFiles()?.forEach { file ->
                    if (file.name.endsWith(".so")) {
                        file.setReadable(true, false)
                        file.setExecutable(true, false)
                    }
                }
            }
            NativeBridge.nativeSetCustomDriver(driver.driverDir, driver.driverLibrary, hookDir)
        }
    }
}
