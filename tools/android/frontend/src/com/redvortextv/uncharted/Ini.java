package com.redvortextv.uncharted;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** A key=value settings file as the launch script reads it (the last line of a key wins). set() replaces a key's
 *  lines and keeps every other line (the in-game menu's keys in bbport.ini), written whole and renamed. */
final class Ini {
    private final File file;

    Ini(File file) { this.file = file; }

    private List<String> lines() {
        List<String> out = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            for (String line; (line = r.readLine()) != null; ) out.add(line);
        } catch (Exception ignored) {}
        return out;
    }

    String get(String key) {
        String value = null;
        for (String line : lines()) if (line.startsWith(key + "=")) value = line.substring(key.length() + 1).trim();
        return value;
    }

    String get(String key, String fallback) {
        String value = get(key);
        return value == null || value.isEmpty() ? fallback : value;
    }

    boolean on(String key, boolean fallback) {
        String value = get(key);
        return value == null ? fallback : value.equals("1");
    }

    synchronized boolean set(String key, String value) {
        List<String> out = new ArrayList<>();
        for (String line : lines()) if (!line.startsWith(key + "=")) out.add(line);
        out.add(key + "=" + value);
        file.getParentFile().mkdirs();
        File temporary = new File(file.getPath() + ".tmp");
        try (Writer w = new OutputStreamWriter(new FileOutputStream(temporary), StandardCharsets.UTF_8)) {
            for (String line : out) w.write(line + "\n");
        } catch (Exception ex) {
            android.util.Log.e("Uncharted", "settings: " + ex);
            return false;
        }
        return temporary.renameTo(file);
    }

    boolean set(String key, boolean value) { return set(key, value ? "1" : "0"); }
}
