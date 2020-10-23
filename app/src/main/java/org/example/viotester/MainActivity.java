package org.example.viotester;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;

import androidx.appcompat.app.AppCompatDelegate;
import androidx.preference.PreferenceManager;

import org.example.viotester.ext_ar.TrackingProvider;
import org.example.viotester.modules.CalibrationActivity;
import org.example.viotester.modules.DataCollectionActivity;
import org.example.viotester.modules.GpuExampleActivity;
import org.example.viotester.modules.TrackingActivity;

public class MainActivity extends Activity {
    private static final String TAG = MainActivity.class.getSimpleName();

    private View.OnClickListener goToActivity(final Class activity) {
        return new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                // start the other activity. permissions should be OK by the time the user
                // manages to click the buttons
                startActivity( new Intent(MainActivity.this, activity));
            }
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        new AssetCopier(this); // copies stuff and saves paths to SharedPreferences

        PreferenceManager.setDefaultValues(this, R.xml.root_preferences, false);

        AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_YES);

        setContentView(R.layout.activity_main);

        for (int i = 0; i < TrackingProvider.ACTIVITY_CLASSES.length; ++i) {
            final Class klass = TrackingProvider.ACTIVITY_CLASSES[i];

            String btnId = "btn_alt_tracking_" + (i+1);
            // crashes if you haven't defined btn_alt_tracking_N
            Button arCoreBtn = findViewById(this.getResources().getIdentifier(btnId, "id", this.getPackageName()));

            arCoreBtn.setText(klass.getSimpleName().replace("Activity", "") + " tracking");
            arCoreBtn.setVisibility(View.VISIBLE);
            arCoreBtn.setOnClickListener(goToActivity(klass));
        }

        if (!BuildConfig.USE_CUSTOM_VIO) {
            findViewById(R.id.btn_tracking).setVisibility(View.GONE);
        }
        if (!BuildConfig.USE_CAMERA_CALIBRATOR) {
            findViewById(R.id.btn_calibration).setVisibility(View.GONE);
        }
        if (!BuildConfig.USE_GPU_EXAMPLES) {
            findViewById(R.id.btn_gpu_examples).setVisibility(View.GONE);
        }
        findViewById(R.id.btn_tracking).setOnClickListener(goToActivity(TrackingActivity.class));
        findViewById(R.id.btn_data_collection).setOnClickListener(goToActivity(DataCollectionActivity.class));
        findViewById(R.id.btn_calibration).setOnClickListener(goToActivity(CalibrationActivity.class));
        findViewById(R.id.btn_gpu_examples).setOnClickListener(goToActivity(GpuExampleActivity.class));
        findViewById(R.id.btn_share).setOnClickListener(goToActivity(ShareListActivity.class));
        findViewById(R.id.btn_settings).setOnClickListener(goToActivity(SettingsActivity.class));

        if (BuildConfig.DEMO_MODE) {
            enableDemoMode();
        }
    }

    private void enableDemoMode() {
        findViewById(R.id.btn_share).setVisibility(View.GONE);
        findViewById(R.id.btn_data_collection).setVisibility(View.GONE);
    }

    @Override
    public void onResume()
    {
        Log.d(TAG, "onResume");
        super.onResume();

        // request camera permission if needed
        if (!PermissionHelper.havePermissions(this)) {
            Log.i(TAG, "asking for user permissions");
            PermissionHelper.requestPermissions(this);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        if (!PermissionHelper.havePermissions(this)) {
            Log.e(TAG, "Camera permission is needed to run this application");
            if (!PermissionHelper.shouldShowRequestPermissionRationale(this)) {
                // Permission denied with checking "Do not ask again".
                PermissionHelper.launchPermissionSettings(this);
            }
            finish();
        }
    }
}