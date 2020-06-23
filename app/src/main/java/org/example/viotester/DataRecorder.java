package org.example.viotester;
import android.os.SystemClock;
import android.util.Log;

import com.google.android.gms.common.util.IOUtils;

import org.kamranzafar.jtar.TarEntry;
import org.kamranzafar.jtar.TarOutputStream;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class DataRecorder {
    private static final String TAG = DataRecorder.class.getName();
    private final File mFolder;
    private final String mVideoFileName;
    private final String mTarFileName;
    private final String mLogFileName;
    private final boolean compress;

    static File getFolder(File cacheDir) {
        return new File(cacheDir, "recordings");
    }

    public DataRecorder(File cacheDir, String prefix, boolean compress) {
        Log.d(TAG, "ctor");

        this.compress = compress;

        final File rootFolder = getFolder(cacheDir);

        if (!rootFolder.exists() && !rootFolder.mkdirs()) {
            throw new RuntimeException("failed to create root folder " + rootFolder.getAbsolutePath());
        }

        SimpleDateFormat dateFormat = new SimpleDateFormat("yyyyMMddHHmmss", Locale.US);
        if (!prefix.isEmpty()) prefix = prefix + "-";
        final String name = prefix + dateFormat.format(new Date());
        mFolder = new File(rootFolder, name);
        if (!mFolder.mkdir()) {
            throw new RuntimeException("failed to create folder " + name);
        }
        mVideoFileName = new File(mFolder, "data.avi").getAbsolutePath();
        mLogFileName = new File(mFolder, "data.jsonl").getAbsolutePath();
        mTarFileName = new File(rootFolder, name + ".tar").getAbsolutePath();

        Log.i(TAG,"video file " + mVideoFileName);
        Log.i(TAG, "sensor log file " + mLogFileName);
    }

    public String getVideoFileName() {
        return mVideoFileName;
    }
    public String getLogFileName() {
        return mLogFileName;
    }

    public void flush() {
        Log.d(TAG, "flush");
        if (this.compress) {
            try {
                writeTarball();
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        }
    }

    private void writeTarball() throws IOException {
        File[] files = mFolder.listFiles();
        if (files == null)
            throw new IOException("failed to list files in " + mFolder.getAbsolutePath());

        try (TarOutputStream out = new TarOutputStream(new BufferedOutputStream(new FileOutputStream(mTarFileName)))) {
            for (File f : files) {
                Log.d(TAG, "adding " + f.getName() + " to tar " + mTarFileName);
                out.putNextEntry(new TarEntry(f, f.getName()));
                try (BufferedInputStream origin = new BufferedInputStream(new FileInputStream(f))) {
                    IOUtils.copyStream(origin, out);
                }
                out.flush();
            }
        }

        Log.i(TAG, "tarball created successfully, clearing folder " + mFolder.getAbsolutePath());
        boolean success = true;
        for (File f : files) if (!f.delete()) success = false;
        if (!mFolder.delete()) success = false;
        if (!success) {
            throw new IOException("failed to clear folder " + mFolder);
        }
    }
}
