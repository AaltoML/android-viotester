package org.example.viotester.modules;

import android.os.Bundle;

import org.example.viotester.AlgorithmActivity;
import org.example.viotester.AlgorithmWorker;

public class CalibrationActivity extends AlgorithmActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        mNativeModule = "calibration";
        super.onCreate(savedInstanceState);
    }

    @Override
    public void adjustSettings(AlgorithmWorker.Settings s) {
        s.recordSensors = false;
        s.recordGps = false;
    }
}
