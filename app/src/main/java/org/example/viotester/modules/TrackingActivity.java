package org.example.viotester.modules;

import android.os.Bundle;

import org.example.viotester.AlgorithmActivity;

public class TrackingActivity extends AlgorithmActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        mRecordPrefix = "vio";
        mNativeModule = "tracking";
        super.onCreate(savedInstanceState);
    }
}
