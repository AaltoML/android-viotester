package org.example.viotester.modules;

import android.graphics.Bitmap;
import android.location.Location;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.widget.CompoundButton;
import android.widget.Switch;

import com.google.android.gms.maps.CameraUpdateFactory;
import com.google.android.gms.maps.GoogleMap;
import com.google.android.gms.maps.OnMapReadyCallback;
import com.google.android.gms.maps.SupportMapFragment;
import com.google.android.gms.maps.model.BitmapDescriptorFactory;
import com.google.android.gms.maps.model.LatLng;
import com.google.android.gms.maps.model.LatLngBounds;
import com.google.android.gms.maps.model.Marker;
import com.google.android.gms.maps.model.MarkerOptions;
import com.google.android.gms.maps.model.Polyline;
import com.google.android.gms.maps.model.PolylineOptions;
import com.google.maps.android.ui.IconGenerator;

import org.example.viotester.AlgorithmActivity;
import org.example.viotester.R;

import java.util.ArrayList;
import java.util.List;

public class MapOverlayActivity extends AlgorithmActivity implements OnMapReadyCallback {
    private static final String TAG = MapOverlayActivity.class.getName();

    private static final int GPS_COLOR = 0xaafc320a;
    private static final int TRACKING_COLOR = 0xaa1099e3;
    private static final float GPS_HUE = BitmapDescriptorFactory.HUE_RED;
    private static final float TRACKING_HUE = BitmapDescriptorFactory.HUE_BLUE;

    private static final long TRACKING_POLL_INTERVAL_MS = 250;
    private static final double START_ALIGN_SECONDS = 5.f;
    private static final double STOP_ALIGN_SECONDS = 15.f;
    private static final float GPS_ACCURACY_THRESHOLD_METERS = 100.0f;

    private GoogleMap googleMap;
    private Aligner aligner;
    private Route gpsRoute;
    private Route trackingRoute;
    Handler handler;
    boolean follow = true;
    Runnable runnable;
    double[] mPose;
    boolean poseStale = false;
    Object poseLock = new Object();
    private double startAlignSeconds;
    private double stopAlignSeconds;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        mRecordPrefix = "vio";
        mNativeModule = "tracking";
        mUseCameraWorker = true;
        mGpsRequired = true;
        mContentView = R.layout.viotester_map_view;
        super.onCreate(savedInstanceState);

        handler = new Handler();

        SupportMapFragment mapFragment =
                (SupportMapFragment) getSupportFragmentManager().findFragmentById(R.id.overlay_map);
        mapFragment.getMapAsync(this);
        mGlSurfaceView.getLayoutParams().width = 1;
        mGlSurfaceView.getLayoutParams().height = 1;

        ((Switch) findViewById(R.id.switch_map_satellite)).setOnCheckedChangeListener(
                new CompoundButton.OnCheckedChangeListener() {
                    public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                        if (googleMap != null) {
                            googleMap.setMapType(isChecked
                                    ? GoogleMap.MAP_TYPE_SATELLITE
                                    : GoogleMap.MAP_TYPE_NORMAL
                            );
                        }
                    }
                });

        ((Switch) findViewById(R.id.switch_map_follow)).setOnCheckedChangeListener(
                new CompoundButton.OnCheckedChangeListener() {
                    public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                        follow = isChecked;
                    }
                });
    }

    @Override
    public void onMapReady(GoogleMap map) {
        Log.d(TAG, "onMapReady");
        googleMap = map;

        // Place initial camera to Helsinki
        final float ZOOM_LEVEL = 0.f;
        googleMap.moveCamera(CameraUpdateFactory.newLatLngZoom(
                new LatLng(60,25), ZOOM_LEVEL));

        IconGenerator ig = new IconGenerator(getApplicationContext());
        ig.setStyle(IconGenerator.STYLE_RED);
        Bitmap bm = ig.makeIcon("GPS");
        Marker gpsMarker = googleMap.addMarker(new MarkerOptions()
                .alpha(.6f)
                .visible(false)
                .position(new LatLng(0,0))
                .icon(BitmapDescriptorFactory.fromBitmap(bm)));
        gpsRoute = new Route(googleMap.addPolyline(new PolylineOptions().color(GPS_COLOR)), gpsMarker);

        ig.setStyle(IconGenerator.STYLE_BLUE);
        ig.setRotation(180);
        ig.setContentRotation(180);
        bm = ig.makeIcon("Tracking");
        Marker trackerMarker = googleMap.addMarker(new MarkerOptions()
                .alpha(.7f)
                .visible(false)
                .anchor(ig.getAnchorU(), ig.getAnchorV())
                .position(new LatLng(0,0))
                .icon(BitmapDescriptorFactory.fromBitmap(bm))
        );
        trackingRoute = new Route(googleMap.addPolyline(new PolylineOptions().color(TRACKING_COLOR)), trackerMarker);
//         test();
    }

    /**
     * For testing purposes only
     */
//    private void test() {
//        InputStream is = this.getResources().openRawResource(R.raw.gps);
//        BufferedReader br = new BufferedReader(new InputStreamReader(is));
//        try {
//            String readLine = null;
//            while ((readLine = br.readLine()) != null) {
//                String[] row = readLine.split(",");
//                gpsRoute.addPoint(
//                        new LatLng(Float.parseFloat(row[1]), Float.parseFloat(row[2])),
//                        Double.parseDouble(row[0]) - 200.);
//            }
//            is.close();
//            br.close();
//        } catch (IOException e) {
//        }
//        gpsRoute.update();
//
//        is = this.getResources().openRawResource(R.raw.track);
//        br = new BufferedReader(new InputStreamReader(is));
//        try {
//            String readLine = null;
//            while ((readLine = br.readLine()) != null) {
//                String[] row = readLine.split(",");
//                double time = Double.parseDouble(row[0]) - 200.;
//                double x = Double.parseDouble(row[1]);
//                double y = Double.parseDouble(row[2]);
//                LatLng newPosition = enuToWgs(gpsRoute.first(), new Point(x, y));
//                trackingRoute.addPoint(newPosition, new Point(-x, y, time));
//            }
//            is.close();
//            br.close();
//        } catch (IOException e) {
//        }
//        trackingRoute.updateAligned(gpsRoute);
//
//        updateCameraPosition();
//    }

    private void updateCameraPosition() {
        final int paddingPixels = 50;
        LatLngBounds.Builder b = new LatLngBounds.Builder();
        if (gpsRoute.coords.size() > 0) {
            b.include(gpsRoute.coords.get(0));
            b.include(gpsRoute.coords.get(gpsRoute.coords.size() - 1));
        }
        if (trackingRoute.coords.size() > 0) {
            b.include(trackingRoute.coords.get(0));
            b.include(trackingRoute.coords.get(trackingRoute.coords.size() - 1));
        }

        googleMap.animateCamera(CameraUpdateFactory.newLatLngBounds(
                b.build(),
                paddingPixels));
    }

    @Override
    public void onResume() {
        super.onResume();
        handler.postDelayed(runnable = new Runnable() {
            public void run() {
                handler.postDelayed(runnable, TRACKING_POLL_INTERVAL_MS);
                synchronized (poseLock) {
                    if (poseStale) {
                        if (mPose != null && mPose[0] > 0.0) {
                            updatePose(mPose.clone());
                        }
                        poseStale = false;
                    }
                }
            }
        }, TRACKING_POLL_INTERVAL_MS);
    }

    @Override
    public void onPause() {
        super.onPause();
        handler.removeCallbacks(runnable);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        handler.removeCallbacks(runnable);
    }

    synchronized protected void onGpsLocationChange(double time, double latitude, double longitude, double altitude, float accuracy) {
        if (googleMap == null && accuracy < GPS_ACCURACY_THRESHOLD_METERS) {
            return;
        }
        LatLng newPosition = new LatLng(latitude, longitude);
        if (gpsRoute.addPoint(newPosition, time)) {
            gpsRoute.updateMap();
            if (follow) {
                updateCameraPosition();
            }
        }
    }

    protected void onPose(double[] pose, int trackingStatus) {
        synchronized (poseLock) {
            if (pose != null && trackingStatus > 0) {
                poseStale = true;
                mPose = pose;
            }
        }
    }

    synchronized private void updatePose(double[] pose) {
        if (googleMap == null || gpsRoute.first() == null) {
            return;
        }
        Point p = new Point(-pose[1], pose[2], pose[0]); // Flip X axis

        if (aligner == null) {
            // Start timings when we have first GPS and Pose coordinates
            startAlignSeconds = START_ALIGN_SECONDS + pose[0];
            stopAlignSeconds = STOP_ALIGN_SECONDS + pose[0];
            aligner = new Aligner(googleMap, startAlignSeconds, stopAlignSeconds);
        }

        boolean updateRequired;

        if (p.time < startAlignSeconds) {
            LatLng newPosition = enuToWgs(gpsRoute.first(), p);
            updateRequired = trackingRoute.addPoint(newPosition, p);

        } else if (p.time > startAlignSeconds && p.time < stopAlignSeconds) {
            LatLng newPosition = enuToWgs(gpsRoute.first(), p);
            trackingRoute.addPoint(newPosition, p);
            aligner.reset(trackingRoute.points, gpsRoute.points);
            List<Point> alignedPoints = aligner.aligned(trackingRoute.points);
            LatLng center = gpsRoute.first();
            List<LatLng> alignedCoords = new ArrayList<>();
            for (Point alignedPoint : alignedPoints) {
                alignedCoords.add(enuToWgs(center, alignedPoint));
            }
            trackingRoute.coords = alignedCoords;
            updateRequired = true;

        } else {
            p = aligner.align(p);
            LatLng newPosition = enuToWgs(gpsRoute.first(), p);
            updateRequired = trackingRoute.addPoint(newPosition, p);
        }

        if (updateRequired) {
            trackingRoute.updateMap();
        }
    }

    private class Point {
        public double time;
        public double x;
        public double y;

        public Point(double x, double y, double time) {
            this.x = x;
            this.y = y;
            this.time = time;
        }

        public Point(double x, double y) {
            this.x = x;
            this.y = y;
        }

        public Point(double time) {
            this.time = time;
            this.x = 0.f;
            this.y = 0.f;
        }
    }

    private class Route {
        private static final float ROUTE_RESOLUTION_IN_METERS = .5f;
        Polyline polyline;
        Marker marker;
        List<LatLng> coords;
        List<Point> points;
        LatLng latestPositon;

        public Route(Polyline polyline, Marker marker) {
            this.polyline = polyline;
            this.marker = marker;
            this.coords = new ArrayList<>();
            this.points = new ArrayList<>();
        }

        /**
         * Return true if point was added, false if not
         */
        public boolean addPoint(LatLng newPosition, Point newPoint) {
            float[] results = new float[1];
            if (latestPositon != null) {
                Location.distanceBetween(latestPositon.latitude, latestPositon.longitude,
                        newPosition.latitude, newPosition.longitude, results);
            } else {
                results[0] = -1.0f;
            }
            if (results[0] < 0.0f || results[0] > ROUTE_RESOLUTION_IN_METERS) {
                points.add(newPoint);
                coords.add(newPosition);
                latestPositon = newPosition;
                return true;
            }
            return false;
        }

        public boolean addPoint(LatLng newPosition, double time) {
            Point p = new Point(time);
            if (coords.size() > 0) {
                Point p2 = wgsToEnu(first(), newPosition);
                p.x = p2.x;
                p.y = p2.y;
            }
            return addPoint(newPosition, p);
        }

        private void updateMap() {
            polyline.setPoints(coords);
            marker.setPosition(coords.get(coords.size() - 1));
            marker.setVisible(true);
        }

        public LatLng first() {
            if (coords.size() > 0) {
                return coords.get(0);
            }
            return null;
        }
    }

    private class Aligner {

        final double start;
        final double stop;

        double[] m;
        double offX;
        double offY;
        double offX2;
        double offY2;

        Marker circleP1s;
        Marker circleP1e;
        Marker circleP2s;
        Marker circleP2e;

        public Aligner(GoogleMap map, double start, double stop) {
            this.start = start;
            this.stop = stop;
            circleP1s = makeMarker(map, TRACKING_HUE);
            circleP1e = makeMarker(map, TRACKING_HUE);
            circleP2s = makeMarker(map, GPS_HUE);
            circleP2e = makeMarker(map, GPS_HUE);
        }

        private Marker makeMarker(GoogleMap map, float color) {
            return map.addMarker(new MarkerOptions()
                    .icon(BitmapDescriptorFactory.defaultMarker(color))
                    .alpha(.5f)
                    .position(new LatLng(0,0))
            );
        }

        public void reset(List<Point> from, List<Point> to) {
            Point p1s = getNearestPoint(from, start);
            Point p1e = getNearestPoint(from, stop);
            Point p2s = getNearestPoint(to, start);
            Point p2e = getNearestPoint(to, stop);

            offX = p1s.x;
            offY = p1s.y;
            offX2 = -p2s.x;
            offY2 = -p2s.y;

            double x1 = p1e.x - p1s.x;
            double x2 = p2e.x - p2s.x;
            double y1 = p1e.y - p1s.y;
            double y2 = p2e.y - p2s.y;
            double a = Math.atan2(x1 * y2 - y1 * x2, x1 * x2 + y1 * y2);
            double[] _m = {Math.cos(a), -Math.sin(a), Math.sin(a), Math.cos(a)};
            m = _m;

            circleP1s.setPosition(enuToWgs(gpsRoute.first(), align(p1s)));
            circleP1e.setPosition(enuToWgs(gpsRoute.first(), align(p1e)));
            circleP2s.setPosition(enuToWgs(gpsRoute.first(), p2s));
            circleP2e.setPosition(enuToWgs(gpsRoute.first(), p2e));
        }

        public List<Point> aligned(List<Point> points) {
            List<Point> alignedPoints = new ArrayList<>();
            for (Point p : points) {
                alignedPoints.add(align(p));
            }
            return alignedPoints;
        }

        public Point align(Point p) {
            double x = p.x - offX;
            double y = p.y - offY;
            return new Point(
                    x * m[0] + y * m[1] - offX2,
                    x * m[2] + y * m[3] - offY2,
                    p.time
            );
        }

        public Point getNearestPoint(List<Point> points, double time) {
            if (points.size() == 0) {
                return null;
            }
            Point closest = points.get(0);
            for (int i = 1; i < points.size(); i++) {
                Point p = points.get(i);
                if (Math.abs(p.time - time) < Math.abs(closest.time - time)) {
                    closest = p;
                } else if (closest.time < time) {
                    break;
                }
            }
            return closest;
        }
    }

    static final double EARTH_R = 6.371e6;
    static final double METERS_PER_LAT = Math.PI * EARTH_R / 180.0;

    private LatLng enuToWgs(LatLng center, Point p) {
        final double metersPerLon = METERS_PER_LAT * Math.cos(center.latitude / 180.0 * Math.PI);
        return new LatLng(
                p.x / METERS_PER_LAT + center.latitude,
                p.y / metersPerLon + center.longitude
        );
    }

    private Point wgsToEnu(LatLng center, LatLng coord) {
        final double metersPerLon = METERS_PER_LAT * Math.cos(center.latitude / 180.0 * Math.PI);
        return new Point(
                (coord.latitude - center.latitude) * METERS_PER_LAT,
                (coord.longitude - center.longitude) * metersPerLon
        );
    }
}
