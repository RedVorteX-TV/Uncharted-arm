package com.redvortextv.uncharted;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import org.json.JSONObject;

/** This port's newest release on GitHub (REPO), asked when the launcher opens (launcher.ini update_check, on
 *  by default). Only the release's tag and page are read: nothing is downloaded or sent, and the player gets
 *  the APK from the release page. Releases are tagged v<versionName> (v0.1.4). */
final class UpdateCheck {
    /** owner/name of the port's repository; empty: no repository yet (no GitHub row, no check). */
    static final String REPO = "";
    static final String YOUTUBE = "https://youtube.com/@redvortex-tv";

    static String repoPage() { return "https://github.com/" + REPO; }
    static String releasesPage() { return repoPage() + "/releases/latest"; }

    static final class Release {
        final String version, page;
        Release(String version, String page) { this.version = version; this.page = page; }
    }

    /** The latest release when it is newer than current, else null (also when the repository has no
     *  release yet or is private: GitHub answers 404). Throws when GitHub cannot be reached. */
    static Release newer(String current) throws IOException {
        if (REPO.isEmpty()) return null;
        HttpURLConnection c = (HttpURLConnection) new URL("https://api.github.com/repos/" + REPO + "/releases/latest").openConnection();
        c.setConnectTimeout(10000); c.setReadTimeout(15000);
        c.setRequestProperty("Accept", "application/vnd.github+json");
        c.setRequestProperty("User-Agent", "Uncharted-Android update check");
        try {
            int code = c.getResponseCode();
            if (code == 404) return null;
            if (code != 200) throw new IOException("GitHub answered " + code);
            ByteArrayOutputStream body = new ByteArrayOutputStream();
            try (InputStream in = c.getInputStream()) {
                byte[] buffer = new byte[8192];
                for (int n; (n = in.read(buffer)) > 0;) { body.write(buffer, 0, n); if (body.size() > 1 << 20) throw new IOException("GitHub's answer is too large"); }
            }
            JSONObject release = new JSONObject(new String(body.toByteArray(), StandardCharsets.UTF_8));
            String tag = release.optString("tag_name", "");
            String version = tag.startsWith("v") ? tag.substring(1) : tag;
            if (release.optBoolean("draft") || release.optBoolean("prerelease") || !isNewer(version, current)) return null;
            return new Release(version, release.optString("html_url", releasesPage()));
        } catch (org.json.JSONException e) {
            throw new IOException("GitHub's answer could not be read");
        } finally {
            c.disconnect();
        }
    }

    /** Whether version a is above b, comparing their numbers in order (0.1.10 > 0.1.9; 0.2 = 0.2.0). */
    static boolean isNewer(String a, String b) {
        String[] x = a.split("[^0-9]+"), y = b.split("[^0-9]+");
        for (int i = 0; i < Math.max(x.length, y.length); i++) {
            long p = number(x, i), q = number(y, i);
            if (p != q) return p > q;
        }
        return false;
    }
    private static long number(String[] parts, int i) {
        if (i >= parts.length || parts[i].isEmpty()) return 0;
        try { return Long.parseLong(parts[i]); } catch (NumberFormatException e) { return 0; }
    }

    private UpdateCheck() {}
}
