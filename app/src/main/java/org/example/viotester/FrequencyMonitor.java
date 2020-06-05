package org.example.viotester;

import android.os.SystemClock;

public class FrequencyMonitor {
    private static final double REPORT_INTERVAL_SECONDS = 2.0;

    public interface Listener {
        void onFrequency(double freq);
    }

    private final Listener mListener;
    private boolean mRunning;
    private long mNSamples;
    private long mLastReportNs;
    private double mLatestFrequency;

    FrequencyMonitor(Listener listener) {
        mListener = listener;
    }

    public void start() {
        mRunning = true;
        mLastReportNs = SystemClock.elapsedRealtimeNanos();
    }

    public void stop() {
        mRunning = false;
    }

    public void onSample() {
        if (mRunning) {
            mNSamples++;
            final long curNs = SystemClock.elapsedRealtimeNanos();
            if (curNs > mLastReportNs + REPORT_INTERVAL_SECONDS * 1e9) {
                final double dt = (curNs - mLastReportNs) * 1e-9;
                mLatestFrequency = mNSamples / dt;
                mListener.onFrequency(mLatestFrequency);
                mLastReportNs = curNs;
                mNSamples = 0;
            }
        }
    }

    public double getLatestFrequency() {
        return mLatestFrequency;
    }
}