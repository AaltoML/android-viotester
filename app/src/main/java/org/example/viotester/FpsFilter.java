package org.example.viotester;

class FpsFilter {
    private long mLast;
    private long mAccumulated;
    private final long mFrameNanos;

    FpsFilter(double fps) {
        mFrameNanos = (long)(1.0 / fps * 1e9);
    }

    void reset() {
        mLast = 0;
        mAccumulated = 0;
    }

    void setTime(long tNanos) {
        if (mLast != 0) {
            mAccumulated += tNanos - mLast;
        }
        mLast = tNanos;
    }

    boolean poll() {
        if (mAccumulated > mFrameNanos) {
            mAccumulated -= mFrameNanos;
            return true;
        }
        return false;
    }

    boolean pollAll() {
        if (mAccumulated > mFrameNanos) {
            mAccumulated -= (mAccumulated / mFrameNanos) * mFrameNanos;
            return true;
        }
        return false;
    }
}
