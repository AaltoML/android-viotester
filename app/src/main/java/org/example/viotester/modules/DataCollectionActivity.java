package org.example.viotester.modules;

import android.os.Bundle;

import org.example.viotester.AlgorithmActivity;

public class DataCollectionActivity extends AlgorithmActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        mDataCollectionMode = true;
        mRecordPrefix = "camera2";
        mNativeModule = "recording";
        super.onCreate(savedInstanceState);
    }
}
