package org.amy.hello;

import android.app.Activity;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.amy.audio.AmyService;

import java.io.File;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final ExecutorService EXECUTOR = Executors.newSingleThreadExecutor();

    private TextView status;
    private Button playButton;

    static {
        System.loadLibrary("amy_hello_client");
    }

    private static native int nativePlayCScale(String socketPath);

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(48, 48, 48, 48);

        TextView title = new TextView(this);
        title.setText("AMY Hello World");
        title.setTextSize(28);
        title.setGravity(Gravity.CENTER);
        root.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        status = new TextView(this);
        status.setText("Starting AMY...");
        status.setTextSize(18);
        status.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams statusParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        statusParams.setMargins(0, 40, 0, 40);
        root.addView(status, statusParams);

        playButton = new Button(this);
        playButton.setText("Play C scale");
        playButton.setOnClickListener(v -> playScale());
        root.addView(playButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        setContentView(root);

        AmyService.start(this);
        if (state == null) {
            playScale();
        } else {
            status.setText("AMY ready");
        }
    }

    private void playScale() {
        playButton.setEnabled(false);
        status.setText("Playing C major scale...");
        String socketPath = new File(getFilesDir(), AmyService.DEFAULT_SOCKET_NAME)
                .getAbsolutePath();

        EXECUTOR.execute(() -> {
            int rc = nativePlayCScale(socketPath);
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                if (rc == 0) {
                    status.setText("C scale complete");
                } else {
                    status.setText("AMY/socket error: " + rc);
                }
                playButton.setEnabled(true);
            });
        });
    }
}
