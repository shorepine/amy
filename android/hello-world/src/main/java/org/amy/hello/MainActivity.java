package org.amy.hello;

import android.app.Activity;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final String TAG = "AmyHelloWorld";
    private static final String SOCKET_NAME = "amy.sock";
    private static final int CONNECT_ATTEMPTS = 100;
    private static final long CONNECT_RETRY_MS = 50;
    private static final ExecutorService EXECUTOR = Executors.newSingleThreadExecutor();
    private static final int[] NOTES = {60, 62, 64, 65, 67, 69, 71, 72};

    private TextView status;
    private Button playButton;

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
        status.setText("Connecting to AMY...");
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

        if (state == null) {
            playScale();
        } else {
            status.setText("AMY socket client ready");
        }
    }

    private void playScale() {
        playButton.setEnabled(false);
        status.setText("Playing C major scale...");
        String socketPath = new File(getFilesDir(), SOCKET_NAME).getAbsolutePath();

        EXECUTOR.execute(() -> {
            try (LocalSocket socket = connectWithRetry(socketPath)) {
                OutputStream output = socket.getOutputStream();

                // Raw oscillator 0, sine wave, full AMY master gain.
                // Every write is one complete AMY wire request and therefore
                // one SOCK_SEQPACKET packet. The client does not call AMY or
                // control the AMY service lifecycle.
                sendWire(output, "v0w0V10.0Z");
                Thread.sleep(30);

                for (int note : NOTES) {
                    sendWire(output, "v0n" + note + "l1Z");
                    Thread.sleep(350);
                    sendWire(output, "v0l0Z");
                    Thread.sleep(80);
                }

                Log.i(TAG, "C scale complete");
                setResultText("C scale complete");
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                Log.e(TAG, "C scale failed: interrupted", ex);
                setResultText("AMY/socket error: interrupted");
            } catch (IOException ex) {
                Log.e(TAG, "C scale failed", ex);
                setResultText("AMY/socket error: " + ex.getMessage());
            }
        });
    }

    private LocalSocket connectWithRetry(String socketPath)
            throws IOException, InterruptedException {
        IOException lastError = null;
        LocalSocketAddress address = new LocalSocketAddress(
                socketPath, LocalSocketAddress.Namespace.FILESYSTEM);

        for (int attempt = 0; attempt < CONNECT_ATTEMPTS; ++attempt) {
            LocalSocket socket = new LocalSocket(LocalSocket.SOCKET_SEQPACKET);
            try {
                socket.connect(address);
                Log.i(TAG, "connected to amy.sock");
                return socket;
            } catch (IOException ex) {
                lastError = ex;
                try {
                    socket.close();
                } catch (IOException ignored) {
                }
                Thread.sleep(CONNECT_RETRY_MS);
            }
        }

        throw new IOException("timed out connecting to amy.sock", lastError);
    }

    private static void sendWire(OutputStream output, String wire) throws IOException {
        byte[] payload = wire.getBytes(StandardCharsets.US_ASCII);
        output.write(payload);
        output.flush();
        Log.i(TAG, "wire: " + wire);
    }

    private void setResultText(String text) {
        runOnUiThread(() -> {
            if (isDestroyed()) return;
            status.setText(text);
            playButton.setEnabled(true);
        });
    }
}
