package org.nx2a.launcher

import android.app.NativeActivity

/**
 * The game runtime: the generated port's NativeActivity, backed by the
 * libdmhost.so built by nx2android (nativeLibraryDir). The data pack lives
 * outside the app, in /sdcard/HogwartsLegacy-data.
 */
class PortActivity : NativeActivity()