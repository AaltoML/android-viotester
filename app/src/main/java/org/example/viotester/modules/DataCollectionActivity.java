package org.example.viotester.modules;

import android.os.Bundle;

import org.example.viotester.AlgorithmActivity;

public class DataCollectionActivity extends AlgorithmActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        mDataCollectionMode = true;
        mNativeModule = "recording";
        super.onCreate(savedInstanceState);
    }
}
