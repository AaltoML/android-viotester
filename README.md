# Visual-Inertial Odometry (VIO) benchmark app for Android

## Usage

There different modes in the application are explained below

### Data collection

Saves data as tarballs to the "external cache" folder on the disk. Accessible via ADB

    # list files
    adb shell ls -lha /storage/emulated/0/Android/data/org.example.viotester/cache/recordings

    # download data (example)
    adb pull /storage/emulated/0/Android/data/org.example.viotester/cache/recordings/20191031104043.tar

These files can also be shared directly from the phone using the _Share recording_ button.
To remove the recordings from the phone, either use ADB (`adb shell rm ...`)
or just clear the cache from Android settings, e.g.,
Long tap icon -> App info -> Storage & Cache -> Clear cache.

For data collection, the recommended settings are
 * Resolution: 1280x960
 * (Target) FPS: 30

TODO: the real recording FPS will not currently be stored to the recorded video,
which may produce speedups in playback. However, this does not otherwise affect the
algorithm and since the true timestamps of the frames are stored in the other recording file, the
problem is fixable later with, e.g, FFMpeg.

### Camera calibration

Approximates camera parameters using OpenCV on phones whose Camera 2 API does provide these values

 1. Print or open [this pattern](https://raw.githubusercontent.com/opencv/opencv/3.4/doc/acircles_pattern.png) on your screen.
 2. Move camera so that the pattern is viewed from different angles and at different locations on the screen (center & edges)
 3. The camera parameters at the current resolution are printed on screen (TODO: share to Slack etc.)

## Linux/Mac setup

On Debian Stretch, these packages needed to be installed with apt-get

 * ninja
 * ccache
 * openjdk-8-jdk

Also an Android SDK and Android NDK need to be installed.

1. Because of the submodules, clone this repo with the `--recursive` Git option (this may take a while)
2. `cd mobile-cv-suite`
3. TODO: should be optional but isn't: run `./scripts/build.sh`
4. Run `./scripts/android/build.sh`
5. Then open the project in Android Studio and run app.

# ARCore test mode

Can be used to compare other methods to Google ARCore.
Enabled by changing the build flavor to `arcore`.

# AREngine test mode

Enables comparison to Huawei AREngine. Enabled by changing the build flavor to `arengine`.
Requirements:
 * Supported Huawei phone
 * Manual download of `HUAWEI AR Engine SDK_???.aar` to `app/src/arengine/libs/` (which is .gitignored).
   Available from https://developer.huawei.com/consumer/en/ar after some registration steps. The
   actual file is located inside the sample code folder in multiple copies, e.g.,
   `./AREnginesdk-sample-2.0.0.6/AREnginesdk-sample-2.0.0.6/java/JavaSample/WorldAR_Java/libs/HUAWEI AR Engine SDK_v2.0.0.aar`.

## Copyright

The files under `app/src/arcore` that say so at the top of the file,
are Licensed under Apache 2 (&copy; Google). They have been copied from the ARCore SDK examples.
To see how they have been modified from their original versions, see comments beginning with "NOTE".

The original code in this repository is also licensed under Apache 2.0.