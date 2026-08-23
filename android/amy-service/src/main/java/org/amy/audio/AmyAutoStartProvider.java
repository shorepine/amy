package org.amy.audio;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;

import java.io.File;

/**
 * Android package lifecycle hook that starts the independent :amy process.
 * Client application code never starts or stops AmyService; clients only
 * connect to filesDir/amy.sock and exchange AMY wire packets.
 */
public final class AmyAutoStartProvider extends ContentProvider {
    @Override
    public boolean onCreate() {
        Context context = getContext();
        if (context == null) return false;

        File socket = new File(context.getFilesDir(), AmyService.DEFAULT_SOCKET_NAME);
        Intent intent = new Intent(context, AmyService.class);
        intent.putExtra(AmyService.EXTRA_SOCKET_PATH, socket.getAbsolutePath());
        context.startService(intent);
        return true;
    }

    @Override public Cursor query(Uri uri, String[] projection, String selection,
                                  String[] selectionArgs, String sortOrder) { return null; }
    @Override public String getType(Uri uri) { return null; }
    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] selectionArgs) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection,
                                String[] selectionArgs) { return 0; }
}
