package org.example.viotester;

public class TrackingOutput {

    public static final int STATUS_INIT = 0;
    public static final int STATUS_TRACKING = 1;
    public static final int STATUS_LOST_TRACKING = 2;

    private double[] output;
    private int status;
    private String statsString;
    private boolean hasPose;

    TrackingOutput(double[] output, int status, String statsString) {
        this.hasPose = output != null;
        if (this.hasPose)
            this.output = output;
        else
            this.output = new double[] {0,0,0,0,0,0,0,0};

        this.status = status;
        this.statsString = statsString;
    }

    public double time() {
        return output[0];
    }

    public double x() {
        return output[1];
    }

    public double y() {
        return output[2];
    }

    public double z() {
        return output[3];
    }

    public double qx() {
        return output[4];
    }

    public double qy() {
        return output[5];
    }

    public double qz() {
        return output[6];
    }

    public double qw() {
        return output[7];
    }

    public int status() {
        return status;
    }

    public String statsString() {
        return statsString;
    }

    public boolean hasPose() {
        return hasPose;
    }

}
