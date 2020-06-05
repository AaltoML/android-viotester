package org.example.viotester;

import android.content.Context;
import android.util.Log;

import com.google.android.gms.common.util.IOUtils;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import androidx.preference.PreferenceManager;

/**
 * Handles copying of Android assets to files in the cache folder so that they can be accessed
 * directly from native code using the file names. Not sure why this is necessary in the first
 * place
 */
public class AssetCopier {
    // changing this string is the easy way to make things work if one changes the build
    // variants that use SLAM
    public static final String HAS_SLAM_FILES_KEY = "has_slam_files_2";
    public static final String ASSET_PATH_KEY_ORB_VOCABULARY = "asset_path_orb_vocabulary";


    private static final String TAG = AssetCopier.class.getSimpleName();
    private final Context mContext;

    AssetCopier(Context context) {
        mContext = context;
        copySlamAssets();
    }

    private void copySlamAssets() {
        String orbVocabPath = copyAssetToCache("orb_vocab.dbow2");

        boolean hasSlamAssets = orbVocabPath != null;

        // hacky: store to shared preferences so that these are easily accessible from
        // any activity that may need them. Alternatively, one could handle all of this only
        // in the relevant actitivy, but then it hard to do it only on app startup and not
        // every time the relevant view is opened. Also at least some of this info may be
        // needed in multiple places, e.g., settings view + algorithm activity
        PreferenceManager.getDefaultSharedPreferences(mContext)
                .edit()
                .putBoolean(HAS_SLAM_FILES_KEY, hasSlamAssets)
                .putString(ASSET_PATH_KEY_ORB_VOCABULARY, orbVocabPath)
                .apply();
    }

    private String copyAssetToCache(String assetName) {
        final File targetFile = new File(mContext.getCacheDir(), "asset_" + assetName);
        final String targetFn = targetFile.getAbsolutePath();
        Log.d(TAG, "copying asset " + assetName + " to " + targetFn);
        try (
                InputStream is = mContext.getAssets().open(assetName);
                OutputStream os = new FileOutputStream(targetFile)) {
            IOUtils.copyStream(is, os);
            return targetFn;
        } catch (IOException e) {
            Log.w(TAG, "Failed to copy asset " + assetName + ": " + e);
            return null;
        }
    }
}
