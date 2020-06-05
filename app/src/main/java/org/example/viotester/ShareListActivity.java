package org.example.viotester;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.text.format.Formatter;
import android.util.Log;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ListView;

import java.io.File;
import java.util.ArrayList;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.FileProvider;

public class ShareListActivity extends AppCompatActivity {
    private static final String TAG = ShareListActivity.class.getName();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_share_list);
        ListView listView = findViewById(R.id.share_data_list);
        listView.setEmptyView(findViewById(R.id.share_data_empty_list));
        populateListView(listView);
    }

    private void populateListView(ListView listView) {
        final ArrayList<String> listItems = new ArrayList<>();
        File[] files = DataRecorder.getFolder(getExternalCacheDir()).listFiles();
        if (files == null) files = new File[0];
        final ArrayList<File> fileList = new ArrayList<>();

        for (File f : files) {
            String fn = f.getName();
            if (fn.endsWith(".tar")) {
                String text = fn + " (" + Formatter.formatShortFileSize(this, f.length()) + ")";
                listItems.add(text);
                fileList.add(f);
            }
        }

        listView.setAdapter(new ArrayAdapter<>(this, R.layout.share_list_item, listItems));
        listView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> adapterView, View view, int i, long l) {
                Log.d(TAG, "selected item " + i + ": " + listItems.get(i));
                File f = fileList.get(i);

                Uri contentUri = FileProvider.getUriForFile(ShareListActivity.this, "org.example.viotester.fileprovider", f);
                if (contentUri == null)
                    throw new RuntimeException("failed to create contentURI for" + f.getAbsolutePath());

                Intent shareIntent = new Intent();
                shareIntent.setAction(Intent.ACTION_SEND);
                shareIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                shareIntent.setDataAndType(contentUri, getContentResolver().getType(contentUri));
                shareIntent.putExtra(Intent.EXTRA_STREAM, contentUri);
                startActivity(Intent.createChooser(shareIntent, "Choose an app for sharing the recording"));
            }
        });
    }

}
