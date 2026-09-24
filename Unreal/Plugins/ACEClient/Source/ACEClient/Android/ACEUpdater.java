package com.acecommunity.updates;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import java.io.File;
import java.io.FileInputStream;
import java.io.OutputStream;
import java.util.Arrays;

/** The platform installer owns consent and atomic APK replacement; game data is never removed. */
public final class ACEUpdater {
    private static boolean busy;
    public static File directory(Context context) {
        File directory = new File(context.getFilesDir(), "ACUpdates");
        directory.mkdirs();
        return directory;
    }
    public static void status(Context context, String value) {
        context.getSharedPreferences("ACUpdate", Context.MODE_PRIVATE).edit().putString("status", value).apply();
    }
    public static String status(Context context) {
        return context.getSharedPreferences("ACUpdate", Context.MODE_PRIVATE).getString("status", "");
    }
    public static synchronized String install(final Activity activity, String path, final int expectedVersion) {
        if (busy) return "started";
        try {
            final File file = new File(path).getCanonicalFile();
            if (!file.equals(new File(directory(activity), "update.apk").getCanonicalFile())) return "failed";
            if (!activity.getPackageManager().canRequestPackageInstalls()) {
                activity.runOnUiThread(() -> {
                    try {
                        activity.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                            Uri.parse("package:" + activity.getPackageName())));
                    } catch (Exception e) { status(activity, "failed-permission"); }
                });
                return "permission";
            }
            busy = true;
            status(activity, "installing");
            new Thread(() -> {
                int sessionId = -1;
                PackageInstaller installer = activity.getPackageManager().getPackageInstaller();
                try {
                    PackageManager manager = activity.getPackageManager();
                    PackageInfo incoming = manager.getPackageArchiveInfo(file.getPath(), PackageManager.GET_SIGNING_CERTIFICATES);
                    PackageInfo current = manager.getPackageInfo(activity.getPackageName(), PackageManager.GET_SIGNING_CERTIFICATES);
                    if (incoming == null || !activity.getPackageName().equals(incoming.packageName)
                        || incoming.getLongVersionCode() != expectedVersion || incoming.getLongVersionCode() <= current.getLongVersionCode())
                        throw new SecurityException("Wrong package or version");
                    Signature[] expected = current.signingInfo.getApkContentsSigners();
                    Signature[] actual = incoming.signingInfo.getApkContentsSigners();
                    if (expected.length != actual.length || expected.length == 0) throw new SecurityException("Wrong signer");
                    for (Signature signature : expected) {
                        boolean found = false;
                        for (Signature other : actual) found |= Arrays.equals(signature.toByteArray(), other.toByteArray());
                        if (!found) throw new SecurityException("Wrong signer");
                    }
                    PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
                    params.setAppPackageName(activity.getPackageName());
                    params.setSize(file.length());
                    if (Build.VERSION.SDK_INT >= 31) params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_REQUIRED);
                    sessionId = installer.createSession(params);
                    try (PackageInstaller.Session session = installer.openSession(sessionId);
                         FileInputStream input = new FileInputStream(file);
                         OutputStream output = session.openWrite("base.apk", 0, file.length())) {
                        byte[] buffer = new byte[262144];
                        int count;
                        while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                        session.fsync(output);
                    }
                    Intent result = new Intent(activity, ACEUpdateReceiver.class);
                    int flags = PendingIntent.FLAG_UPDATE_CURRENT;
                    if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
                    PendingIntent pending = PendingIntent.getBroadcast(activity, sessionId, result, flags);
                    try (PackageInstaller.Session session = installer.openSession(sessionId)) {
                        session.commit(pending.getIntentSender());
                    }
                } catch (Exception e) {
                    if (sessionId >= 0) try { installer.abandonSession(sessionId); } catch (Exception ignored) {}
                    status(activity, "failed-install");
                } finally { synchronized (ACEUpdater.class) { busy = false; } }
            }, "ACUpdateInstall").start();
            return "started";
        } catch (Exception e) { status(activity, "failed-install"); return "failed"; }
    }
}
