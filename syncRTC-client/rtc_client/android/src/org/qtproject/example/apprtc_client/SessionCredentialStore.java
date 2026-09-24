package org.qtproject.example.apprtc_client;

import android.content.Context;
import android.content.SharedPreferences;

import org.qtproject.qt.android.QtNative;

public final class SessionCredentialStore {
    private static final String PREFERENCES_NAME = "syncrtc_remembered_session";

    private SessionCredentialStore() {
    }

    private static SharedPreferences preferences() {
        return QtNative.getContext().getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE);
    }

    private static String accountKey(String targetName) {
        return targetName + ".account";
    }

    private static String sessionTokenKey(String targetName) {
        return targetName + ".session_token";
    }

    public static boolean save(String targetName, String account, String sessionToken) {
        final SharedPreferences preferences = preferences();
        return preferences.edit()
                .putString(accountKey(targetName), account)
                .putString(sessionTokenKey(targetName), sessionToken)
                .commit();
    }

    public static String loadAccount(String targetName) {
        final SharedPreferences preferences = preferences();
        return preferences.getString(accountKey(targetName), "");
    }

    public static String loadSessionToken(String targetName) {
        final SharedPreferences preferences = preferences();
        return preferences.getString(sessionTokenKey(targetName), "");
    }

    public static boolean clear(String targetName) {
        final SharedPreferences preferences = preferences();
        return preferences.edit()
                .remove(accountKey(targetName))
                .remove(sessionTokenKey(targetName))
                .commit();
    }
}
