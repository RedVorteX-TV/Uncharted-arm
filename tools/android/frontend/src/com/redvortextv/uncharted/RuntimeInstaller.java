package com.redvortextv.uncharted;

import android.content.Context;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.function.Consumer;

/** Installs the APK's runtime (the Bionic game process, the preparation tool and the launch script) in this app's
 *  private data directory, when the APK's runtime digest differs from the installed one. Adapted from ducvd89's
 *  Bloodborne-Android (RuntimeInstaller). */
final class RuntimeInstaller {
    private static byte[] readAll(InputStream in) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int count;
        while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
        return out.toByteArray();
    }

    private static String hex(byte[] bytes) {
        StringBuilder out = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) out.append(String.format("%02x", b & 255));
        return out.toString();
    }

    /** The runtime's key files are there and executable. */
    static boolean installed(File files) {
        return new File(files, "bbport/bionic/bin/bb-probe").canExecute() &&
                new File(files, "bbport/bionic/bin/bb-prepare").canExecute() &&
                new File(files, "bbport/run-uncharted.sh").isFile();
    }

    /** Recursively deletes a tree without following links. */
    static boolean deleteTree(File file) {
        if (!file.exists() && !Files.isSymbolicLink(file.toPath())) return true;
        if (file.isDirectory() && !Files.isSymbolicLink(file.toPath())) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) if (!deleteTree(child)) return false;
        }
        return file.delete();
    }

    static synchronized void ensure(Context context, File files, Consumer<String> status) throws Exception {
        String expected;
        try (InputStream in = context.getAssets().open("runtime.sha256")) {
            expected = new String(readAll(in), StandardCharsets.US_ASCII).trim();
        } catch (java.io.FileNotFoundException missing) {
            if (installed(files)) return;
            throw new IllegalStateException("This APK does not contain the runtime", missing);
        }
        if (!expected.matches("[0-9a-f]{64}")) throw new IllegalStateException("Invalid runtime digest");
        File marker = new File(files, ".runtime.sha256");
        if (marker.isFile() && installed(files) &&
                expected.equals(new String(Files.readAllBytes(marker.toPath()), StandardCharsets.US_ASCII).trim()))
            return;

        status.accept(Strings.get("installing_runtime"));
        File archive = new File(files, "runtime.tar.gz.part");
        MessageDigest sha = MessageDigest.getInstance("SHA-256");
        try {
            try (InputStream in = context.getAssets().open("runtime.payload");
                 FileOutputStream out = new FileOutputStream(archive)) {
                byte[] buffer = new byte[1024 * 1024];
                int count;
                while ((count = in.read(buffer)) != -1) {
                    sha.update(buffer, 0, count);
                    out.write(buffer, 0, count);
                }
                out.getFD().sync();
            }
            if (!expected.equals(hex(sha.digest()))) throw new IllegalStateException("Runtime archive checksum mismatch");
            // The bundled trees are replaced wholesale: no stale libraries beside the new ones.
            if (!deleteTree(new File(files, "bbport/bionic"))) throw new IllegalStateException("Cannot remove the old runtime");
            Process tar = new ProcessBuilder("/system/bin/tar", "-oxzf", archive.getAbsolutePath(),
                    "-C", files.getAbsolutePath()).redirectErrorStream(true).start();
            byte[] log;
            try (InputStream in = tar.getInputStream()) { log = readAll(in); }
            if (tar.waitFor() != 0) throw new IllegalStateException("Runtime extraction failed: " +
                    new String(log, 0, Math.min(log.length, 2048), StandardCharsets.UTF_8));
            if (!installed(files)) throw new IllegalStateException("Runtime archive is incomplete");
            Files.write(marker.toPath(), (expected + "\n").getBytes(StandardCharsets.US_ASCII));
        } finally {
            archive.delete();
        }
    }

    private RuntimeInstaller() {}
}
