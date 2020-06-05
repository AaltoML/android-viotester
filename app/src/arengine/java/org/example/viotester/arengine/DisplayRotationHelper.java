/*
 * Copyright 2017 Google Inc. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// NOTE: this file is based on the Huawei AREngine sample app source code,
// which is based on the ARCore sample source code but the changes are not
// clearly marked. In this file, the changes are marked with respect to the
// Huawei sample code
package org.example.viotester.arengine; // NOTE: changed package name
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.hardware.display.DisplayManager.DisplayListener;
import android.view.Display;
import android.view.WindowManager;

import com.huawei.hiar.ARSession;

public class DisplayRotationHelper implements DisplayListener{

    private boolean mViewportChanged;
    private int mViewportWidth;
    private int mViewportHeight;
    private final Context mContext;
    private final Display mDisplay;


    public DisplayRotationHelper(Context context) {
        this.mContext = context;
        mDisplay = context.getSystemService(WindowManager.class).getDefaultDisplay();
    }

    public void onResume() {
        mContext.getSystemService(DisplayManager.class).registerDisplayListener(this, null);
    }

    public void onPause() {
        mContext.getSystemService(DisplayManager.class).unregisterDisplayListener(this);
    }

    public void onSurfaceChanged(int width, int height) {
        mViewportWidth = width;
        mViewportHeight = height;
        mViewportChanged = true;
    }

    public void updateSessionIfNeeded(ARSession session) {
        if (mViewportChanged) {
            int displayRotation = mDisplay.getRotation();
            session.setDisplayGeometry(displayRotation, mViewportWidth, mViewportHeight);
            mViewportChanged = false;
        }
    }

    public int getRotation() {
        return mDisplay.getRotation();
    }

    @Override
    public void onDisplayAdded(int displayId) {

    }

    @Override
    public void onDisplayRemoved(int displayId) {

    }

    @Override
    public void onDisplayChanged(int displayId) {
        mViewportChanged = true;
    }
}