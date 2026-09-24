package com.acecommunity.updates;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;

/** Explicit, non-exported result receiver also survives an Activity recreation. */
public final class ACEUpdateReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent result) {
        int status = result.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirm = result.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm != null) {
                try {
                    confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    context.startActivity(confirm);
                    ACEUpdater.status(context, "confirm");
                    return;
                } catch (Exception ignored) {}
            }
        } else if (status == PackageInstaller.STATUS_SUCCESS) {
            ACEUpdater.status(context, "success");
            return;
        }
        ACEUpdater.status(context, "failed-" + status);
    }
}
