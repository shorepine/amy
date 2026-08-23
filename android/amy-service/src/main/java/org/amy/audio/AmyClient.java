package org.amy.audio;

import android.content.Context;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Minimal pure-Java client for the private AMY SOCK_SEQPACKET socket.
 *
 * This class is transport only: it contains no AMY engine code, does not start
 * or stop AmyService, and does not load any JNI/native client library.
 */
public final class AmyClient {
    private static final int EINVAL = 22;
    private static final int EIO = 5;
    private static final int ENOTCONN = 107;

    private static LocalSocket socket;
    private static OutputStream output;

    private AmyClient() {}

    private static String socketPath(Context context) {
        if (context == null) return "";
        return new File(context.getFilesDir(), AmyService.DEFAULT_SOCKET_NAME)
                .getAbsolutePath();
    }

    /** Connect once to filesDir/amy.sock. Returns 0 or a negative errno-style value. */
    public static synchronized int connect(Context context) {
        if (context == null) return -EINVAL;
        closeLocked();

        LocalSocket candidate = new LocalSocket(LocalSocket.SOCKET_SEQPACKET);
        LocalSocketAddress address = new LocalSocketAddress(
                socketPath(context), LocalSocketAddress.Namespace.FILESYSTEM);
        try {
            candidate.connect(address);
            output = candidate.getOutputStream();
            socket = candidate;
            return 0;
        } catch (IOException ex) {
            try {
                candidate.close();
            } catch (IOException ignored) {
            }
            return -EIO;
        }
    }

    /** Send exactly one AMY wire command as one SOCK_SEQPACKET packet. */
    public static synchronized int sendWire(String wire) {
        if (socket == null || output == null) return -ENOTCONN;
        if (wire == null || wire.isEmpty()) return -EINVAL;

        try {
            output.write(wire.getBytes(StandardCharsets.US_ASCII));
            output.flush();
            return 0;
        } catch (IOException ex) {
            closeLocked();
            return -EIO;
        }
    }

    public static synchronized void close() {
        closeLocked();
    }

    private static void closeLocked() {
        output = null;
        if (socket != null) {
            try {
                socket.close();
            } catch (IOException ignored) {
            }
            socket = null;
        }
    }
}
