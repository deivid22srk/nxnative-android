package org.nx2a.launcher

import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.view.Surface

/** JNI bridge. All validation happens in C++ (app/src/main/cpp). */
object Native {
    init {
        System.loadLibrary("nxnative_android")
    }

    external fun nativeVersion(): String
    external fun inspectNsp(fd: Int): String
    external fun loadKeys(path: String): String
    external fun analyzeImport(fd: Int, keysPath: String): String
    external fun rendererStart(surface: Surface, width: Int, height: Int): String
    external fun rendererStop()
    external fun rendererStats(): String
    external fun rendererInput(action: Int, x: Float, y: Float, code: Int)

    fun analyze(context: Context, uri: Uri, keysPath: String): ImportReport {
        context.contentResolver.openFileDescriptor(uri, "r")?.use { pfd ->
            return parseImportReport(Native.analyzeImport(pfd.fd, keysPath))
        }
        return ImportReport(false, "ERR_IO", "cannot open the selected file", "", -1, "", 0,
            emptyList(), emptyList(), 0, -1, false)
    }

    fun analyzePfd(pfd: ParcelFileDescriptor, keysPath: String): ImportReport =
        parseImportReport(Native.analyzeImport(pfd.fd, keysPath))
}
