package org.example.viotester;

import android.content.SharedPreferences;
import android.os.Bundle;
import android.text.InputType;
import android.util.Log;
import android.view.View;
import android.widget.EditText;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.preference.EditTextPreference;
import androidx.preference.ListPreference;
import androidx.preference.Preference;
import androidx.preference.PreferenceFragmentCompat;
import androidx.preference.PreferenceManager;

public class SettingsActivity extends AppCompatActivity {
    static private final String DEFAULT_RESO = "640x480";
    static private final String DEFAULT_FPS = "30";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(SettingsActivity.this );
        setContentView(R.layout.settings_activity);
        getSupportFragmentManager()
                .beginTransaction()
                .replace(R.id.settings, new SettingsFragment(
                        prefs.getStringSet("camera_set", new TreeSet<String>()),
                        prefs.getStringSet("resolution_set", new TreeSet<String>()),
                        prefs.getStringSet("fps_set", new TreeSet<String>()),
                        prefs.getBoolean("has_auto_focal_length", false),
                        prefs.getBoolean(AssetCopier.HAS_SLAM_FILES_KEY, false) && BuildConfig.USE_SLAM
                ))
                .commit();
    }

    private static class ResolutionComparator implements Comparator<String> {
        @Override
        public int compare(String r1, String r2) {
            String[] dims1 = r1.split("x"), dims2 = r2.split("x");
            int xDiff = Integer.parseInt(dims1[0]) - Integer.parseInt(dims2[0]);
            int yDiff = Integer.parseInt(dims1[1]) - Integer.parseInt(dims2[1]);
            if (xDiff != 0) return xDiff;
            return yDiff;
        }
    }

    public static class SettingsFragment extends PreferenceFragmentCompat {
        private final Set<String> mCameraSet;
        private final Set<String> mResolutionSet;
        private final Set<String> mFpsSet;
        private final boolean mHasAutoFocalLength;
        private final boolean mSlamPossible;

        SettingsFragment(Set<String> cameraSet, Set<String> resolutionSet, Set<String> fpsSet, boolean hasAutoFocalLength, boolean slamPossible) {
            mCameraSet = cameraSet;
            mResolutionSet = resolutionSet;
            mHasAutoFocalLength = hasAutoFocalLength;
            mSlamPossible = slamPossible;
            mFpsSet = fpsSet;
        }

        @Override
        public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
            setPreferencesFromResource(R.xml.root_preferences, rootKey);
            populateCameras();
            populateResolutions();
            populateFps();
            populateVideoVisualizations();
            populateOverlayVisualizations();
            setFocalLength();
            findPreference("enable_slam").setEnabled(mSlamPossible);

            if (BuildConfig.DEMO_MODE) {
                enableDemoMode();
            }
        }

        String[] DEMO_MODE_SETTINGS = {
            "visualization",
            "overlay_visualization"
        };
        Set<String> DEMO_MODE_SETTINGS_SET = new HashSet<>(Arrays.asList(DEMO_MODE_SETTINGS));
        private void enableDemoMode() {
            for (String key : getPreferenceScreen().getSharedPreferences().getAll().keySet()) {
                if (DEMO_MODE_SETTINGS_SET.contains(key)) {
                    continue;
                }
                Preference pref = findPreference(key);
                if (pref != null) {
                    findPreference(key).setVisible(false);
                }
            }
        }

        private void populateCameras() {
            // Get the Preference Category which we want to add the ListPreference to
            ListPreference pref = findPreference("target_camera");

            ArrayList<String> cameras = new ArrayList<>();
            final CharSequence[] entries, entryValues;

            if (mCameraSet.isEmpty()) {
                entries = new CharSequence[]{"open tracking view to populate!"};
                entryValues = new CharSequence[]{ "0" };
            } else {
                cameras.addAll(mCameraSet);

                entries = new CharSequence[cameras.size()];
                entryValues = new CharSequence[cameras.size()];

                Collections.sort(cameras);

                for (int i = 0; i < cameras.size(); ++i) {
                    entries[i] = cameras.get(i);
                    entryValues[i] = cameras.get(i);
                }
                pref.setDefaultValue(cameras.get(0));
            }

            pref.setEntries(entries);
            pref.setEntryValues(entryValues);

            if (mCameraSet.size() == 1) {
                pref.setEnabled(false);
            }
        }

        private void populateResolutions() {
            // Get the Preference Category which we want to add the ListPreference to
            ListPreference resolutionListPref = findPreference("target_size");

            ArrayList<String> resolutions = new ArrayList<>();
            final CharSequence[] entries, entryValues;

            if (mResolutionSet.isEmpty()) {
                entries = new CharSequence[]{"open tracking view to populate!"};
                entryValues = new CharSequence[]{ DEFAULT_RESO };
            } else {
                resolutions.addAll(mResolutionSet);

                entries = new CharSequence[resolutions.size()];
                entryValues = new CharSequence[resolutions.size()];

                Collections.sort(resolutions, new ResolutionComparator());

                for (int i = 0; i < resolutions.size(); ++i) {
                    entries[i] = resolutions.get(i);
                    entryValues[i] = resolutions.get(i);
                }
            }

            resolutionListPref.setEntries(entries);
            resolutionListPref.setEntryValues(entryValues);
            resolutionListPref.setDefaultValue(DEFAULT_RESO);
        }

        private void populateFps() {
            // Get the Preference Category which we want to add the ListPreference to
            ListPreference resolutionListPref = findPreference("fps");
            ArrayList<String> fps = new ArrayList<>();
            final CharSequence[] entries, entryValues;
            if (mFpsSet.isEmpty()) {
                entries = new CharSequence[]{"open tracking view to populate!"};
                entryValues = new CharSequence[]{ DEFAULT_FPS };
            } else {
                fps.addAll(mFpsSet);
                Collections.sort(fps, new Comparator<String>() {
                    @Override
                    public int compare(String r1, String r2) {
                        return Integer.compare(Integer.parseInt(r1), Integer.parseInt(r2));
                    }
                });
                entries = new CharSequence[fps.size()];
                entryValues = new CharSequence[fps.size()];

                for (int i = 0; i < fps.size(); ++i) {
                    entries[i] = fps.get(i);
                    entryValues[i] = fps.get(i);
                }
            }
            resolutionListPref.setEntries(entries);
            resolutionListPref.setEntryValues(entryValues);
            resolutionListPref.setDefaultValue(DEFAULT_RESO);
        }

        private void populateVideoVisualizations() {
            ListPreference pref = findPreference("visualization");
            AlgorithmWorker.Settings.VideoVisualization[] options = AlgorithmWorker.Settings.VideoVisualization.values();
            final CharSequence[] entries = new CharSequence[options.length];
            for (int i=0; i<options.length; ++i) {
                entries[i] = options[i].name().toLowerCase();
            }

            pref.setEntries(entries);
            pref.setEntryValues(entries);
            pref.setDefaultValue("plain_video");
        }

        private void populateOverlayVisualizations() {
            ListPreference pref = findPreference("overlay_visualization");
            AlgorithmWorker.Settings.OverlayVisualization[] options = AlgorithmWorker.Settings.OverlayVisualization.values();
            final CharSequence[] entries = new CharSequence[options.length];
            for (int i=0; i<options.length; ++i) {
                entries[i] = options[i].name().toLowerCase();
            }

            pref.setEntries(entries);
            pref.setEntryValues(entries);
            pref.setDefaultValue("track");
        }

        private void setFocalLength() {
            // Get the Preference Category which we want to add the ListPreference to
            EditTextPreference focalLength = findPreference("focal_length_1280");
            if (mHasAutoFocalLength) {
                focalLength.setEnabled(false);
            }
            else {
                // only allow numeric input
                focalLength.setOnBindEditTextListener(new EditTextPreference.OnBindEditTextListener() {
                    @Override
                    public void onBindEditText(@NonNull EditText editText) {
                        editText.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL);
                    }
                });
            }
        }
    }
}
