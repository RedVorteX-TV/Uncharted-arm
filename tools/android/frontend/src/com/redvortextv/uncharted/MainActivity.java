package com.redvortextv.uncharted;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputFilter;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.AbsListView;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/** Uncharted: The Nathan Drake Collection for Android (ARM port by RedVorteX-TV). A launcher with the Linux
 *  launcher's choices (the game, 720p, FSR 1, the FPS limit, the compatibility switches, the GPU driver, the
 *  language), then the game on a surface under a sliding menu. The game process, its frame path, its sound and its
 *  pad come from ducvd89's Bloodborne-Android (BbPresent, AudioBridge, PadBridge, TouchControllerOverlay,
 *  the driver classes). */
public final class MainActivity extends Activity {
    private String FILES, BASE;
    private Ini bbport, launcher;
    private String gameDir;
    private static final int REQUEST_FOLDER = 1, REQUEST_STORAGE = 2, REQUEST_DRIVER = 3, REQUEST_LSFG = 4;
    private static final int GOLD = 0xffc6ad76, TEXT = 0xffe8dfc9, MUTED = 0xff8e9097, CARD = 0xff1b1d24, BACK = 0xff0f1014;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();

    // Game layer
    private FrameLayout root, gameLayer;
    private SurfaceView surface;
    private TextView status, pausedChip;
    private View handle, scrim;
    private LinearLayout drawer, drawerItems, pauseItem, controllerItem;
    private TouchControllerOverlay controllerOverlay;
    private PadBridge pad;
    private AudioBridge audio;
    // Launcher layer
    private ScrollView launcherView;
    private LinearLayout launcherBody;
    private TextView launcherStatus, launcherNotice;
    private View playButton;
    private final List<View> gameCards = new ArrayList<>();

    private volatile Process game;
    private static final int SIGSTOP = 19, SIGCONT = 18;
    private final Object pauseLock = new Object();
    private int gamePid;
    private boolean userPaused, backgrounded, stopped;
    private volatile boolean runtimeReady, displayReady, destroyed, hadError, playing;
    private boolean drawerOpen, selectDown, imeOpen, imePolling;
    private final AtomicBoolean imeAnswered = new AtomicBoolean();
    private DriverManager drivers;
    private DriverDownloadDialog driverDownload;
    private volatile GpuFamily gpu;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        FILES = getFilesDir().getAbsolutePath();
        BASE = FILES + "/bbport";
        new File(BASE).mkdirs();
        bbport = new Ini(new File(BASE, "bbport.ini"));
        launcher = new Ini(new File(BASE, "launcher.ini"));
        Strings.setLanguage(launcher.get("language", "auto"));
        drivers = new DriverManager(new File(FILES));
        gameDir = getSharedPreferences("uncharted", MODE_PRIVATE).getString("game_dir", null);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams attrs = getWindow().getAttributes();
            attrs.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(attrs);
        }
        pad = new PadBridge(new File(BASE, "android-pad.state"));
        audio = new AudioBridge(new File(BASE, "audio.fifo"));
        audio.start();
        buildGameLayer();
        buildLauncher();
        setContentView(root);
        immersive();
        showLauncher(null);
        worker.execute(this::installRuntime);
        if (!storageGranted()) requestStorage();
        checkForUpdate();
    }

    /** A newer release on GitHub (UpdateCheck) becomes a row under the status line that opens its page. */
    private void checkForUpdate() {
        if (UpdateCheck.REPO.isEmpty() || !launcher.on("update_check", true)) return;
        String current = versionName();
        Thread check = new Thread(() -> {
            UpdateCheck.Release release;
            try { release = UpdateCheck.newer(current); }
            catch (Exception e) { android.util.Log.i("Uncharted", "update check: " + DriverCatalog.message(e)); return; }
            if (release == null) return;
            main.post(() -> {
                if (destroyed) return;
                LinearLayout row = actionRow(Strings.format("update_available", release.version),
                    Strings.format("update_available_hint", current), () -> openLink(release.page));
                launcherBody.removeView(row);
                launcherBody.addView(row, launcherBody.indexOfChild(launcherNotice) + 1);
            });
        }, "update check");
        check.setDaemon(true);
        check.start();
    }

    private String versionName() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
        catch (Exception e) { return ""; }
    }

    /** Opens a web page in the browser (the YouTube app takes the channel's link when installed). */
    private void openLink(String url) {
        try { startActivity(new Intent(Intent.ACTION_VIEW, android.net.Uri.parse(url))); }
        catch (android.content.ActivityNotFoundException e) { toast(url); }
    }

    // ---------------------------------------------------------------------------------------------
    // Small view helpers
    private int dp(float n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    private TextView text(String value, float size, int color) {
        TextView t = new TextView(this); t.setText(value); t.setTextSize(size); t.setTextColor(color); return t;
    }
    private static FrameLayout.LayoutParams frame(int width, int height, int gravity) {
        return new FrameLayout.LayoutParams(width, height, gravity);
    }
    private GradientDrawable rounded(int color, int radius, int stroke, int strokeColor) {
        GradientDrawable g = new GradientDrawable(); g.setColor(color); g.setCornerRadius(dp(radius));
        if (stroke > 0) g.setStroke(dp(stroke), strokeColor);
        return g;
    }
    /** A focusable background: gold edge when focused (gamepad), lighter when pressed. */
    private StateListDrawable focusable(int idle, int radius) {
        StateListDrawable states = new StateListDrawable();
        states.addState(new int[]{android.R.attr.state_pressed}, rounded(0x44c6ad76, radius, 2, GOLD));
        states.addState(new int[]{android.R.attr.state_focused}, rounded(0x33c6ad76, radius, 2, GOLD));
        states.addState(new int[]{}, rounded(idle, radius, 0, 0));
        return states;
    }
    private void toast(String message) { android.widget.Toast.makeText(this, message, android.widget.Toast.LENGTH_LONG).show(); }

    // ---------------------------------------------------------------------------------------------
    // The game layer: the surface (16:9, black bars), the touch controller, the menu drawer
    private void buildGameLayer() {
        root = new FrameLayout(this); root.setBackgroundColor(Color.BLACK);
        gameLayer = new FrameLayout(this);
        root.addView(gameLayer, frame(-1, -1, Gravity.FILL));
        surface = new SurfaceView(this);
        gameLayer.addView(surface, frame(-1, -1, Gravity.CENTER));
        gameLayer.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> fitSurface(r - l, b - t));
        surface.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder h) { if (runtimeReady) startDisplay(); }
            @Override public void surfaceChanged(SurfaceHolder h, int format, int w, int height) {}
            @Override public void surfaceDestroyed(SurfaceHolder h) {}
        });
        surface.setOnTouchListener((v, e) -> true);
        status = text("", 18, GOLD); status.setGravity(Gravity.CENTER); status.setPadding(dp(40), 0, dp(40), 0);
        gameLayer.addView(status, frame(-1, -1, Gravity.CENTER));

        controllerOverlay = new TouchControllerOverlay(this, pad);
        controllerOverlay.setMiniMap(false);
        controllerOverlay.setVisibility(View.INVISIBLE);
        gameLayer.addView(controllerOverlay, frame(-1, -1, Gravity.CENTER));

        handle = new View(this);
        handle.setBackground(rounded(0x80c6ad76, 4, 0, 0));
        gameLayer.addView(handle, frame(dp(4), dp(44), Gravity.LEFT | Gravity.CENTER_VERTICAL));
        handle.setOnClickListener(v -> openDrawer());

        pausedChip = text(Strings.get("paused_chip"), 13, TEXT); pausedChip.setPadding(dp(14), dp(7), dp(14), dp(7));
        pausedChip.setBackground(rounded(0xcc121318, 16, 1, 0x99c6ad76));
        pausedChip.setVisibility(View.GONE);
        pausedChip.setOnClickListener(v -> togglePause());
        FrameLayout.LayoutParams chip = frame(-2, -2, Gravity.TOP | Gravity.CENTER_HORIZONTAL); chip.topMargin = dp(16);
        gameLayer.addView(pausedChip, chip);

        scrim = new View(this); scrim.setBackgroundColor(0x99000000);
        scrim.setVisibility(View.GONE); scrim.setOnClickListener(v -> closeDrawer());
        gameLayer.addView(scrim, frame(-1, -1, Gravity.FILL));

        drawer = new LinearLayout(this); drawer.setOrientation(LinearLayout.VERTICAL);
        drawer.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[]{0xff1b1d24, 0xff121318}));
        drawer.setElevation(dp(16)); drawer.setVisibility(View.GONE);
        drawerItems = new LinearLayout(this); drawerItems.setOrientation(LinearLayout.VERTICAL);
        drawerItems.setPadding(dp(14), dp(20), dp(14), dp(16));
        ScrollView scroll = new ScrollView(this); scroll.addView(drawerItems);
        drawer.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        TextView title = text("Uncharted", 28, TEXT); title.setTypeface(Typeface.SERIF, Typeface.BOLD);
        title.setPadding(dp(10), 0, 0, 0);
        drawerItems.addView(title);
        TextView hint = text(Strings.get("menu_hint"), 12, MUTED); hint.setPadding(dp(10), dp(2), 0, dp(10));
        drawerItems.addView(hint);
        drawerRow("ic_play", Strings.get("back_to_game"), null, this::closeDrawer);
        pauseItem = drawerRow("ic_pause", Strings.get("pause"), "", this::togglePause);
        drawerRow("ic_refresh", Strings.get("restart"), null, () -> confirm(Strings.get("restart_question"), this::restartGame));
        drawerRow("ic_exit", Strings.get("quit_to_launcher"), null, () -> confirm(Strings.get("quit_question"), () -> {
            closeDrawer(); worker.execute(() -> { stopGame(); main.post(() -> showLauncher(null)); });
        }));
        controllerItem = drawerRow("ic_gamepad", Strings.get("touch_controls"), "", () -> {
            launcher.set("touch_controls", !touchControls()); controllerOverlay.release(); closeDrawer();
        });
        drawerRow("ic_edit", Strings.get("edit_layout"), null, () -> {
            launcher.set("touch_controls", true); closeDrawer(); controllerOverlay.edit(null);
        });
        TextView credit = text(Strings.get("credit"), 11, 0xff6f7178); credit.setPadding(dp(24), dp(4), dp(14), dp(14));
        drawer.addView(credit);
        gameLayer.addView(drawer, frame(dp(320), -1, Gravity.LEFT));
    }

    private LinearLayout drawerRow(String icon, String title, String detail, Runnable action) {
        LinearLayout row = new LinearLayout(this); row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(10), dp(8), dp(10), dp(8)); row.setMinimumHeight(dp(48));
        row.setFocusable(true); row.setClickable(true); row.setOnClickListener(v -> action.run());
        row.setBackground(focusable(0, 10));
        android.widget.ImageView image = new android.widget.ImageView(this); image.setTag("icon");
        int id = getResources().getIdentifier(icon, "drawable", getPackageName());
        if (id != 0) image.setImageResource(id);
        LinearLayout.LayoutParams ip = new LinearLayout.LayoutParams(dp(24), dp(24)); ip.rightMargin = dp(14);
        row.addView(image, ip);
        LinearLayout texts = new LinearLayout(this); texts.setOrientation(LinearLayout.VERTICAL);
        TextView name = text(title, 15, TEXT); name.setTag("title"); texts.addView(name);
        if (detail != null) { TextView second = text(detail, 12, MUTED); second.setTag("detail"); texts.addView(second); }
        row.addView(texts, new LinearLayout.LayoutParams(0, -2, 1));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2); lp.bottomMargin = dp(2);
        drawerItems.addView(row, lp);
        return row;
    }
    private static void setDetail(LinearLayout row, String value) {
        TextView detail = row == null ? null : (TextView) row.findViewWithTag("detail");
        if (detail != null) detail.setText(value);
    }

    private int[] fitSize(int width, int height) {
        double aspect = 16.0 / 9.0;
        int w = width, h = height;
        if (w > h * aspect) w = (int) Math.round(h * aspect); else h = (int) Math.round(w / aspect);
        return new int[]{Math.max(1, Math.min(width, w)), Math.max(1, Math.min(height, h))};
    }
    private void fitSurface(int width, int height) {
        if (width <= 0 || height <= 0) return;
        int[] fit = fitSize(width, height);
        FrameLayout.LayoutParams p = (FrameLayout.LayoutParams) surface.getLayoutParams();
        if (p.width == fit[0] && p.height == fit[1]) return;
        p.width = fit[0]; p.height = fit[1];
        surface.post(() -> surface.setLayoutParams(p));
    }

    // ---------------------------------------------------------------------------------------------
    // The launcher
    private void buildLauncher() {
        launcherView = new ScrollView(this);
        launcherView.setBackgroundColor(BACK);
        launcherView.setFillViewport(true);
        FrameLayout center = new FrameLayout(this);
        launcherBody = new LinearLayout(this); launcherBody.setOrientation(LinearLayout.VERTICAL);
        launcherBody.setPadding(dp(24), dp(20), dp(24), dp(28));
        center.addView(launcherBody, frame(Math.min(dp(820), getResources().getDisplayMetrics().widthPixels), -2, Gravity.CENTER_HORIZONTAL));
        launcherView.addView(center);
        root.addView(launcherView, frame(-1, -1, Gravity.FILL));

        // Header
        LinearLayout header = new LinearLayout(this); header.setGravity(Gravity.BOTTOM);
        TextView title = text("UNCHARTED", 34, TEXT); title.setTypeface(Typeface.SERIF, Typeface.BOLD);
        title.setLetterSpacing(0.08f);
        header.addView(title);
        TextView subtitle = text("  " + Strings.get("subtitle"), 15, GOLD); subtitle.setPadding(0, 0, 0, dp(6));
        header.addView(subtitle);
        launcherBody.addView(header);
        TextView credit = text(Strings.get("credit"), 12, MUTED);
        launcherBody.addView(credit);

        // The game
        section(Strings.get("game"));
        LinearLayout cards = new LinearLayout(this);
        String[][] games = {{"collection", "collection_detail"}, {"u2", "u2_detail"}, {"u3", "u3_detail"}};
        gameCards.clear();
        for (String[] g : games) {
            LinearLayout card = new LinearLayout(this); card.setOrientation(LinearLayout.VERTICAL);
            card.setPadding(dp(14), dp(14), dp(14), dp(14)); card.setFocusable(true); card.setClickable(true);
            card.setTag(g[0]);
            TextView name = text(Strings.get(g[0]), 17, TEXT); name.setTypeface(Typeface.SERIF, Typeface.BOLD);
            card.addView(name);
            card.addView(text(Strings.get(g[1]), 12, MUTED));
            card.setOnClickListener(v -> { launcher.set("game", g[0]); refreshCards(); });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, -2, 1);
            lp.rightMargin = g == games[games.length - 1] ? 0 : dp(10);
            cards.addView(card, lp);
            gameCards.add(card);
        }
        launcherBody.addView(cards);
        refreshCards();

        TextView play = text(Strings.get("play"), 20, 0xff15161a);
        play.setTypeface(Typeface.DEFAULT_BOLD); play.setGravity(Gravity.CENTER); play.setLetterSpacing(0.1f);
        play.setFocusable(true); play.setClickable(true); play.setPadding(0, dp(14), 0, dp(14));
        StateListDrawable playStates = new StateListDrawable();
        playStates.addState(new int[]{android.R.attr.state_pressed}, rounded(0xffd8c189, 12, 0, 0));
        playStates.addState(new int[]{android.R.attr.state_focused}, rounded(0xffe0cb97, 12, 3, 0xfffff3d0));
        playStates.addState(new int[]{}, rounded(GOLD, 12, 0, 0));
        play.setBackground(playStates);
        play.setOnClickListener(v -> play());
        LinearLayout.LayoutParams pp = new LinearLayout.LayoutParams(-1, -2); pp.topMargin = dp(14);
        launcherBody.addView(play, pp);
        playButton = play;
        launcherStatus = text("", 13, MUTED); launcherStatus.setPadding(dp(4), dp(8), 0, 0);
        launcherBody.addView(launcherStatus);
        launcherNotice = text("", 13, 0xffe0a070); launcherNotice.setPadding(dp(4), dp(4), 0, 0);
        launcherNotice.setVisibility(View.GONE);
        launcherBody.addView(launcherNotice);

        // Graphics: shared with the in-game menu through bbport.ini
        section(Strings.get("graphics"));
        switchRow(bbport, "render_720p", true, Strings.get("render_720p"), Strings.get("render_720p_hint"));
        switchRow(bbport, "fsr1", true, Strings.get("fsr1"), Strings.get("fsr1_hint"));
        cyclerRow(bbport, "fps_limit", Strings.get("frame_rate"), Strings.get("frame_rate_hint"),
            new String[]{"30", "40", "45", "60", "0"}, new String[]{Strings.get("fps_30"), Strings.get("fps_40"),
                Strings.get("fps_45"), Strings.get("fps_60"), Strings.get("fps_0")}, "40");
        // Frame generation (bbport.ini, like the in-game menu); LSFG with the player's own Lossless.dll
        cyclerRow(bbport, "frame_gen", Strings.get("frame_gen"), Strings.get("frame_gen_hint"),
            new String[]{"off", "fsr3", "lsfg"}, new String[]{Strings.get("frame_gen_off"), "FSR 3", "LSFG"}, "off");
        cyclerRow(bbport, "frame_gen_multiplier", Strings.get("frame_gen_multiplier"), Strings.get("frame_gen_multiplier_hint"),
            new String[]{"2", "3", "4"}, new String[]{"2×", "3×", "4×"}, "2");
        actionRow(Strings.get("lsfg_dll"), lsfgSummary(), this::askLsfgDll).setTag("lsfg");

        // Compatibility (ARM): launcher.ini (the in-game menu rewrites bbport.ini whole)
        section(Strings.get("compatibility"));
        for (String key : new String[]{"arm_uma_readback", "arm_flush_draws", "arm_job_spin"})
            switchRow(launcher, key, true, Strings.get(key), Strings.get(key + "_hint"));
        switchRow(launcher, "vblank_120", false, Strings.get("vblank_120"), Strings.get("vblank_120_hint"));
        actionRow(Strings.get("recommended"), Strings.get("recommended_hint"), this::applyRecommended);

        // System
        section(Strings.get("system"));
        actionRow(Strings.get("gpu_driver"), driverSummary(), this::showDrivers).setTag("driver");
        actionRow(Strings.get("game_folder"), gameDir == null ? Strings.get("not_chosen") : gameDir, () -> askGameFolder()).setTag("folder");
        switchRow(launcher, "perf_stats", false, Strings.get("perf_stats"), Strings.get("perf_stats_hint"));
        actionRow(Strings.get("logs"), Strings.get("logs_hint") + "\n" + sharedLogDir().getPath(), () -> {
            File log = new File(sharedLogDir(), "android-game.log");
            toast(log.isFile() ? log.getPath() : Strings.get("no_log"));
        });
        switchRow(launcher, "touch_controls", false, Strings.get("touch_controls"), Strings.get("touch_controls_hint"));
        actionRow(Strings.get("edit_layout"), null, () -> {
            launcher.set("touch_controls", true);
            launcherView.setVisibility(View.GONE);
            controllerOverlay.setVisibility(View.VISIBLE);
            controllerOverlay.edit(() -> { controllerOverlay.setVisibility(View.INVISIBLE); showLauncher(null); });
        });
        String language = launcher.get("language", "auto");
        cyclerRow(launcher, "language", Strings.get("language"), null, new String[]{"auto", "en", "es"},
            new String[]{Strings.get("lang_auto"), "English", "Español"}, "auto");
        launcherView.setTag(language);

        // The port: its author's channel, its repository and its updates
        section(Strings.get("project"));
        actionRow(Strings.get("youtube"), Strings.get("youtube_hint"), () -> openLink(UpdateCheck.YOUTUBE));
        if (!UpdateCheck.REPO.isEmpty()) {
            actionRow(Strings.get("github"), Strings.get("github_hint"), () -> openLink(UpdateCheck.repoPage()));
            switchRow(launcher, "update_check", true, Strings.get("update_check"), Strings.get("update_check_hint"));
        }

        TextView controls = text(Strings.get("controls_hint"), 12, MUTED); controls.setPadding(dp(4), dp(18), 0, 0);
        launcherBody.addView(controls);
        String version = " " + versionName();
        TextView credits = text(Strings.get("credits") + version + ": " + Strings.get("credits_text"), 11, 0xff6f7178);
        credits.setPadding(dp(4), dp(10), 0, 0);
        launcherBody.addView(credits);
    }

    private void section(String name) {
        TextView t = text(name.toUpperCase(java.util.Locale.ROOT), 12, GOLD);
        t.setLetterSpacing(0.15f); t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setPadding(dp(4), dp(22), 0, dp(8));
        launcherBody.addView(t);
    }

    private LinearLayout row(String title, String hint) {
        LinearLayout row = new LinearLayout(this); row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(16), dp(12), dp(16), dp(12)); row.setMinimumHeight(dp(56));
        row.setFocusable(true); row.setClickable(true);
        row.setBackground(focusable(CARD, 12));
        LinearLayout texts = new LinearLayout(this); texts.setOrientation(LinearLayout.VERTICAL);
        texts.addView(text(title, 15, TEXT));
        if (hint != null) { TextView h = text(hint, 12, MUTED); h.setTag("hint"); h.setPadding(0, dp(3), dp(12), 0); texts.addView(h); }
        row.addView(texts, new LinearLayout.LayoutParams(0, -2, 1));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2); lp.bottomMargin = dp(6);
        launcherBody.addView(row, lp);
        return row;
    }

    /** A setting that is on or off: the row toggles it (A on a controller), the pill shows it. */
    private LinearLayout switchRow(Ini ini, String key, boolean fallback, String title, String hint) {
        LinearLayout row = row(title, hint);
        TextView pill = text("", 13, 0xff15161a); pill.setTypeface(Typeface.DEFAULT_BOLD);
        pill.setGravity(Gravity.CENTER); pill.setMinWidth(dp(56)); pill.setPadding(dp(10), dp(4), dp(10), dp(4));
        row.addView(pill);
        Runnable show = () -> {
            boolean on = ini.on(key, fallback);
            pill.setText(on ? Strings.get("on") : Strings.get("off"));
            pill.setTextColor(on ? 0xff15161a : TEXT);
            pill.setBackground(rounded(on ? GOLD : 0xff2c2f38, 14, 0, 0));
        };
        show.run();
        row.setTag(show);
        row.setOnClickListener(v -> { ini.set(key, !ini.on(key, fallback)); show.run(); });
        return row;
    }

    /** A setting with several values: the row (or right) goes to the next one, left to the previous one. */
    private LinearLayout cyclerRow(Ini ini, String key, String title, String hint, String[] values, String[] names, String fallback) {
        LinearLayout row = row(title, hint);
        TextView value = text("", 14, GOLD); value.setTypeface(Typeface.DEFAULT_BOLD);
        row.addView(value);
        Runnable show = () -> {
            String current = ini.get(key, fallback);
            int at = java.util.Arrays.asList(values).indexOf(current);
            value.setText("‹  " + names[Math.max(0, at)] + "  ›");
        };
        show.run();
        row.setTag(show);
        java.util.function.IntConsumer step = delta -> {
            int at = Math.max(0, java.util.Arrays.asList(values).indexOf(ini.get(key, fallback)));
            String next = values[(at + delta + values.length) % values.length];
            ini.set(key, next);
            show.run();
            if (key.equals("language")) { Strings.setLanguage(next); recreate(); }
        };
        row.setOnClickListener(v -> step.accept(1));
        row.setOnKeyListener((v, code, e) -> {
            if (code != KeyEvent.KEYCODE_DPAD_LEFT && code != KeyEvent.KEYCODE_DPAD_RIGHT) return false;
            if (e.getAction() == KeyEvent.ACTION_DOWN) step.accept(code == KeyEvent.KEYCODE_DPAD_RIGHT ? 1 : -1);
            return true;
        });
        return row;
    }

    private LinearLayout actionRow(String title, String hint, Runnable action) {
        LinearLayout row = row(title, hint);
        TextView arrow = text("›", 22, GOLD);
        row.addView(arrow);
        row.setOnClickListener(v -> action.run());
        return row;
    }

    private void setRowHint(String tag, String value) {
        View row = launcherBody.findViewWithTag(tag);
        TextView hint = row == null ? null : (TextView) row.findViewWithTag("hint");
        if (hint != null) hint.setText(value);
    }

    private void refreshCards() {
        String chosen = launcher.get("game", "collection");
        for (View card : gameCards) {
            boolean on = chosen.equals(card.getTag());
            StateListDrawable states = new StateListDrawable();
            states.addState(new int[]{android.R.attr.state_pressed}, rounded(0x44c6ad76, 12, 2, GOLD));
            states.addState(new int[]{android.R.attr.state_focused}, rounded(on ? 0x55c6ad76 : 0x33c6ad76, 12, 3, 0xfffff3d0));
            states.addState(new int[]{}, rounded(on ? 0x44c6ad76 : CARD, 12, on ? 2 : 0, GOLD));
            card.setBackground(states);
        }
    }

    /** Every launcher setting back to what runs best on an ARM handheld (the Linux launcher's button). */
    private void applyRecommended() {
        bbport.set("render_720p", true); bbport.set("fsr1", true); bbport.set("fps_limit", "40");
        for (String key : new String[]{"arm_uma_readback", "arm_flush_draws", "arm_job_spin"}) launcher.set(key, true);
        launcher.set("vblank_120", false);
        refreshRows(launcherBody);
        notice(Strings.get("recommended_done"));
    }
    private void refreshRows(View view) {
        if (view.getTag() instanceof Runnable) ((Runnable) view.getTag()).run();
        if (view instanceof android.view.ViewGroup) {
            android.view.ViewGroup group = (android.view.ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) refreshRows(group.getChildAt(i));
        }
    }
    private void notice(String message) {
        launcherNotice.setText(message); launcherNotice.setVisibility(View.VISIBLE);
    }

    private void showLauncher(String message) {
        if (destroyed) return;
        closeDrawer();
        refreshRows(launcherBody);
        setRowHint("driver", driverSummary());
        setRowHint("folder", gameDir == null ? Strings.get("not_chosen") : gameDir);
        if (message != null) notice(message); else launcherNotice.setVisibility(View.GONE);
        controllerOverlay.release();
        controllerOverlay.setVisibility(View.INVISIBLE);
        handle.setVisibility(View.GONE);
        launcherView.setVisibility(View.VISIBLE);
        playButton.requestFocus();
        pad.reset();
    }
    private boolean launcherShown() { return launcherView.getVisibility() == View.VISIBLE; }

    // ---------------------------------------------------------------------------------------------
    // Runtime, display and the game process
    private void installRuntime() {
        try {
            RuntimeInstaller.ensure(this, new File(FILES), message -> main.post(() -> launcherStatus.setText(message)));
            new File(FILES, "home/bbport").mkdirs();
            new File(FILES, "tmp").mkdirs();
            runtimeReady = true;
            main.post(() -> {
                launcherStatus.setText("");
                if (surface.getHolder().getSurface().isValid()) startDisplay();
            });
        } catch (Exception ex) {
            android.util.Log.e("Uncharted", "runtime: " + ex);
            main.post(() -> launcherStatus.setText("Runtime: " + ex.getMessage()));
        }
    }

    private void startDisplay() {
        if (displayReady || destroyed || !surface.getHolder().getSurface().isValid()) return;
        String result = BbPresent.start(surface.getHolder(), getFilesDir() + "/bbpresent.sock");
        android.util.Log.i("Uncharted", result);
        if (!result.startsWith("bridge running:")) { launcherStatus.setText(result); return; }
        displayReady = true;
        pollFrames();
        if (!imePolling) { imePolling = true; pollIme(); }
    }

    private void play() {
        if (!runtimeReady) { toast(Strings.get("installing_runtime")); return; }
        if (!displayReady) { startDisplay(); if (!displayReady) return; }
        if (gameDir == null || !isGame(new File(gameDir)) || !storageGranted()) { askGameFolder(); return; }
        launcherView.setVisibility(View.GONE);
        handle.setVisibility(View.VISIBLE);
        status.setText(Strings.get("starting")); status.setVisibility(View.VISIBLE);
        applyOverlayVisibility();
        surface.requestFocus();
        worker.execute(this::startGame);
    }

    private void restartGame() {
        closeDrawer();
        status.setText(Strings.get("starting")); status.setVisibility(View.VISIBLE);
        worker.execute(() -> { stopGame(); startGame(); });
    }

    private Process launchScript(List<String> extra) throws java.io.IOException {
        ProcessBuilder b = new ProcessBuilder("/system/bin/sh", BASE + "/run-uncharted.sh", "game").redirectErrorStream(true);
        Map<String, String> env = b.environment(); env.clear();
        env.put("PATH", "/system/bin");
        env.put("HOME", FILES + "/home/bbport"); env.put("LANG", "C.UTF-8");
        env.put("TMPDIR", FILES + "/tmp");
        env.put("FXD_FILES", FILES);
        if (gameDir != null) env.put("BB_GAME_DIR", gameDir);
        for (String pair : extra) { int eq = pair.indexOf('='); env.put(pair.substring(0, eq), pair.substring(eq + 1)); }
        return b.start();
    }

    private void startGame() {
        if (destroyed) return;
        hadError = false; playing = false;
        releasePause(false);
        gameState(true);
        new File(BASE, "ime.request").delete(); new File(BASE, "ime.request.result").delete();
        try {
            List<String> extra = new ArrayList<>(), log = new ArrayList<>();
            String chosen = launcher.get("game", "collection");
            extra.add("BB_EXE=" + (chosen.equals("u2") ? "u2" : chosen.equals("u3") ? "u3" : "eboot.bin"));
            // Automatic GPU driver: the bundled one for this GPU (installed from the APK on first use) or the phone's own.
            if (gpuSetting().equals("auto")) {
                AutoDriver.Result auto = AutoDriver.resolve(this, drivers, gpu(), Build.VERSION.SDK_INT,
                    message -> main.post(() -> status.setText(message)));
                log.add(auto.line);
                extra.add("BB_GPU_DRIVER=" + (auto.id == null ? "system" : auto.id));
                extra.add("BB_GPU_DRIVER_WHY=" + auto.line);
            }
            int w = surface.getWidth(), h = surface.getHeight();
            if (gameLayer.getWidth() > 0 && gameLayer.getHeight() > 0) {
                int[] fit = fitSize(gameLayer.getWidth(), gameLayer.getHeight()); w = fit[0]; h = fit[1];
            }
            extra.add("BB_SCREEN_W=" + (w > 0 ? w : 1920)); extra.add("BB_SCREEN_H=" + (h > 0 ? h : 1080));
            File gameLog = new File(BASE, "logs/android-game.log"), previous = new File(BASE, "logs/android-game.prev.log");
            gameLog.getParentFile().mkdirs();
            if (gameLog.length() > 0) { previous.delete(); gameLog.renameTo(previous); }
            final Process launched = launchScript(extra);
            game = launched;
            synchronized (pauseLock) { gamePid = pidOf(launched); }
            main.post(this::applyPause);
            Thread reader = new Thread(() -> {
                boolean driverTrouble = false;
                // A copy in Download/Uncharted, where the player can open it and send it (the app's own
                // folder is private).
                PrintWriter shared = null;
                try {
                    File dir = sharedLogDir(); dir.mkdirs();
                    File copy = new File(dir, "android-game.log"), prev = new File(dir, "android-game.prev.log");
                    if (copy.length() > 0) { prev.delete(); copy.renameTo(prev); }
                    shared = new PrintWriter(copy);
                } catch (Exception ex) { android.util.Log.w("Uncharted", "shared log: " + ex); }
                try (BufferedReader r = new BufferedReader(new InputStreamReader(launched.getInputStream()));
                     PrintWriter out = new PrintWriter(gameLog)) {
                    for (String line : log) { out.println(line); if (shared != null) shared.println(line); }
                    out.flush(); if (shared != null) shared.flush();
                    for (String line; (line = r.readLine()) != null; ) {
                        out.println(line); out.flush();
                        if (shared != null) { shared.println(line); shared.flush(); }
                        if (!driverTrouble && driverTrouble(line)) driverTrouble = true;
                        if (line.startsWith("Preparing ") || line.startsWith("Game image: preparing"))
                            main.post(() -> status.setText(Strings.get("preparing")));
                    }
                    int code = launched.waitFor();
                    if (game == launched) {
                        game = null; releasePause(false);
                        String message = code == 0 ? null
                            : Strings.format("game_stopped", code) + (driverTrouble ? Strings.get("driver_trouble") : "");
                        main.post(() -> { gameState(false); showLauncher(message); });
                    }
                } catch (Exception ex) {
                    if (!destroyed && game == launched) error(ex.toString());
                } finally {
                    if (shared != null) shared.close();
                }
            }, "Uncharted-log");
            reader.setDaemon(true); reader.start();
        } catch (Exception ex) { error(ex.toString()); }
    }

    /** Download/Uncharted in shared storage: copies of the game's log for the player. */
    static File sharedLogDir() {
        return new File(android.os.Environment.getExternalStoragePublicDirectory(
            android.os.Environment.DIRECTORY_DOWNLOADS), "Uncharted");
    }

    static boolean driverTrouble(String line) {
        return line.contains("Device lost") || line.contains("DeviceLost")
            || line.startsWith("Vulkan: cannot load driver") || line.startsWith("Vulkan: no system loader")
            || line.startsWith("Vulkan: driver ") || line.contains("Failed to create device")
            || line.contains("ErrorInitializationFailed") || line.contains("ErrorIncompatibleDriver")
            || ((line.startsWith("Host fault") || line.startsWith("  return address")) && line.contains("libvulkan"));
    }

    private void error(String message) {
        hadError = true;
        android.util.Log.e("Uncharted", message);
        main.post(() -> showLauncher(message));
    }

    private void stopGame() {
        Process p = game; game = null;
        releasePause(true);
        playing = false;
        if (p != null) { p.destroyForcibly(); try { p.waitFor(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); } }
    }

    private void pollFrames() {
        if (destroyed) return;
        if (!hadError && game != null && BbPresent.framesIfLoaded() > 0 && status.getVisibility() == View.VISIBLE) {
            status.setVisibility(View.GONE);
            if (!playing) { playing = true; gameState(false); }
        }
        main.postDelayed(this::pollFrames, 500);
    }

    /** Tells the OS (GameManager, Android 13+) whether the game is loading or being played. */
    private void gameState(boolean loading) {
        if (Build.VERSION.SDK_INT < 33) return;
        try {
            android.app.GameManager gm = getSystemService(android.app.GameManager.class);
            if (gm != null) gm.setGameState(new android.app.GameState(loading, android.app.GameState.MODE_GAMEPLAY_UNINTERRUPTIBLE));
        } catch (RuntimeException e) { android.util.Log.w("Uncharted", "GameManager.setGameState: " + e); }
    }

    private static int pidOf(Process p) {
        String text = String.valueOf(p);
        int at = text.indexOf("pid=");
        if (at >= 0) {
            int end = at + 4; while (end < text.length() && Character.isDigit(text.charAt(end))) ++end;
            try { return Integer.parseInt(text.substring(at + 4, end)); } catch (NumberFormatException ignored) {}
        }
        for (Class<?> c = p.getClass(); c != null; c = c.getSuperclass()) {
            try {
                java.lang.reflect.Field f = c.getDeclaredField("pid"); f.setAccessible(true);
                return f.getInt(p);
            } catch (ReflectiveOperationException | RuntimeException ignored) {}
        }
        return 0;
    }

    // ---------------------------------------------------------------------------------------------
    // Pause, the menu drawer, the touch controller
    private void applyPause() {
        synchronized (pauseLock) {
            boolean hold = userPaused || backgrounded, stop = hold && gamePid > 0;
            if (stop && !stopped) { pad.reset(); android.os.Process.sendSignal(gamePid, SIGSTOP); }
            else if (!stop && stopped) android.os.Process.sendSignal(gamePid, SIGCONT);
            stopped = stop;
            if (audio != null) { if (hold) audio.pause(); else audio.resume(); }
        }
        updatePauseUi();
    }
    private void togglePause() {
        synchronized (pauseLock) { if (gamePid <= 0) return; userPaused = !userPaused; }
        applyPause();
    }
    private void releasePause(boolean signal) {
        synchronized (pauseLock) {
            if (signal && stopped && gamePid > 0) android.os.Process.sendSignal(gamePid, SIGCONT);
            gamePid = 0; userPaused = false; stopped = false;
            if (audio != null && !backgrounded) audio.resume();
        }
        main.post(() -> { if (!destroyed) updatePauseUi(); });
    }
    private void updatePauseUi() {
        boolean paused, running;
        synchronized (pauseLock) { paused = userPaused; running = gamePid > 0; }
        if (pauseItem != null) {
            ((android.widget.ImageView) pauseItem.findViewWithTag("icon")).setImageResource(
                getResources().getIdentifier(paused ? "ic_play" : "ic_pause", "drawable", getPackageName()));
            ((TextView) pauseItem.findViewWithTag("title")).setText(paused ? Strings.get("resume") : Strings.get("pause"));
            pauseItem.setAlpha(running ? 1f : 0.45f);
        }
        pausedChip.setVisibility(paused && !drawerOpen && !launcherShown() ? View.VISIBLE : View.GONE);
        if (paused) controllerOverlay.release();
        applyOverlayVisibility();
    }
    private boolean touchControls() { return launcher.on("touch_controls", false); }
    private void applyOverlayVisibility() {
        boolean paused;
        synchronized (pauseLock) { paused = userPaused; }
        boolean show = !launcherShown() && !drawerOpen && (controllerOverlay.isEditing() || (touchControls() && !paused));
        controllerOverlay.setVisibility(show ? View.VISIBLE : View.INVISIBLE);
    }
    private void openDrawer() {
        if (drawerOpen || launcherShown()) return;
        drawerOpen = true; controllerOverlay.release(); pad.reset();
        setDetail(controllerItem, touchControls() ? Strings.get("on") : Strings.get("off"));
        updatePauseUi();
        handle.setVisibility(View.GONE);
        scrim.setAlpha(0); scrim.setVisibility(View.VISIBLE); scrim.animate().alpha(1).setDuration(200).start();
        drawer.setTranslationX(-dp(320)); drawer.setVisibility(View.VISIBLE);
        drawer.animate().translationX(0).setDuration(220).start();
        drawerItems.getChildAt(2).requestFocus();
    }
    private void closeDrawer() {
        if (!drawerOpen) return;
        drawerOpen = false; pad.reset();
        updatePauseUi();
        scrim.animate().alpha(0).setDuration(180).withEndAction(() -> scrim.setVisibility(View.GONE)).start();
        drawer.animate().translationX(-dp(320)).setDuration(200).withEndAction(() -> {
            drawer.setVisibility(View.GONE);
            if (!launcherShown()) { handle.setVisibility(View.VISIBLE); surface.requestFocus(); }
        }).start();
        immersive();
    }
    private void confirm(String question, Runnable yes) {
        AlertDialog d = new AlertDialog.Builder(this).setMessage(question)
            .setNegativeButton(Strings.get("no"), null)
            .setPositiveButton(Strings.get("yes"), (dialog, which) -> yes.run()).create();
        show(d);
    }

    // ---------------------------------------------------------------------------------------------
    // Input: the launcher and the menus are driven by the controller (D-pad/stick moves the focus, A chooses,
    // B goes back); in the game the pad goes to the game (PadBridge), Back/Select+Start opens the menu.
    @Override public boolean dispatchKeyEvent(KeyEvent e) {
        int code = e.getKeyCode();
        boolean up = e.getAction() == KeyEvent.ACTION_UP;
        if (code == KeyEvent.KEYCODE_BUTTON_SELECT) selectDown = e.getAction() == KeyEvent.ACTION_DOWN;
        if (launcherShown() || drawerOpen) {
            if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_BUTTON_B || code == KeyEvent.KEYCODE_BUTTON_MODE) {
                if (up) { if (drawerOpen) closeDrawer(); else if (code == KeyEvent.KEYCODE_BACK) moveTaskToBack(true); }
                return true;
            }
            if (code == KeyEvent.KEYCODE_BUTTON_A) {
                View focus = getCurrentFocus();
                if (up && focus != null) focus.performClick();
                return true;
            }
            if (code == KeyEvent.KEYCODE_BUTTON_START && launcherShown()) { if (up) play(); return true; }
            return super.dispatchKeyEvent(e);
        }
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_BUTTON_MODE
                || (code == KeyEvent.KEYCODE_BUTTON_START && selectDown)) {
            if (up) openDrawer();
            if (code == KeyEvent.KEYCODE_BUTTON_START) pad.reset();
            return true;
        }
        if (userPaused) return e.isFromSource(android.view.InputDevice.SOURCE_GAMEPAD) || super.dispatchKeyEvent(e);
        if (pad.key(e)) return true;
        return super.dispatchKeyEvent(e);
    }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent e) {
        // The launcher and the menu: the framework turns the stick and the D-pad into focus moves.
        if (launcherShown() || drawerOpen) return super.dispatchGenericMotionEvent(e);
        if (userPaused && e.isFromSource(android.view.InputDevice.SOURCE_JOYSTICK)) return true;
        return pad.motion(e) || super.dispatchGenericMotionEvent(e);
    }
    @Override public void onBackPressed() { if (drawerOpen) closeDrawer(); else if (!launcherShown()) openDrawer(); else moveTaskToBack(true); }

    /** Shows a dialog that a controller can answer: A clicks the focused button or list row, B cancels. */
    private void show(AlertDialog dialog) {
        dialog.setOnKeyListener((d, code, e) -> {
            if (e.getAction() != KeyEvent.ACTION_UP) return code == KeyEvent.KEYCODE_BUTTON_A;
            if (code == KeyEvent.KEYCODE_BUTTON_A) {
                View focus = dialog.getCurrentFocus();
                if (focus instanceof AbsListView) {
                    AbsListView list = (AbsListView) focus;
                    View selected = list.getSelectedView();
                    if (selected != null) list.performItemClick(selected, list.getSelectedItemPosition(), list.getSelectedItemId());
                } else if (focus != null) focus.performClick();
                return true;
            }
            if (code == KeyEvent.KEYCODE_BUTTON_B) { dialog.cancel(); return true; }
            return false;
        });
        dialog.setOnDismissListener(d -> immersive());
        dialog.show();
    }

    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN |
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                c.hide(WindowInsets.Type.systemBars());
            }
        }
    }
    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) immersive(); else if (pad != null) pad.reset();
    }
    @Override protected void onPause() {
        if (controllerOverlay != null) controllerOverlay.release();
        if (pad != null) pad.reset();
        synchronized (pauseLock) { backgrounded = true; }
        applyPause();
        super.onPause();
    }
    @Override protected void onResume() {
        super.onResume();
        synchronized (pauseLock) { backgrounded = false; }
        applyPause();
    }

    // ---------------------------------------------------------------------------------------------
    // The game's text dialog (BB_IME_FILE): shown here with the touch keyboard
    private void pollIme() {
        if (destroyed) return;
        File request = new File(BASE, "ime.request");
        if (!imeOpen && request.isFile()) {
            try (BufferedReader r = new BufferedReader(new InputStreamReader(new java.io.FileInputStream(request), "UTF-8"))) {
                int max = Integer.parseInt(r.readLine().trim());
                String title = r.readLine(), initial = r.readLine();
                request.delete();
                showIme(Math.max(1, max), title == null ? "" : title, initial == null ? "" : initial);
            } catch (Exception ex) { android.util.Log.e("Uncharted", "text request: " + ex); request.delete(); }
        }
        main.postDelayed(this::pollIme, 250);
    }
    private void showIme(int max, String title, String initial) {
        imeOpen = true; imeAnswered.set(false); pad.reset(); closeDrawer();
        EditText field = new EditText(this);
        field.setSingleLine(true); field.setText(initial); field.setSelection(field.getText().length());
        field.setFilters(new InputFilter[]{new InputFilter.LengthFilter(max)});
        field.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        FrameLayout box = new FrameLayout(this); box.setPadding(dp(20), dp(8), dp(20), 0); box.addView(field);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle(title.isEmpty() ? "Text" : title).setView(box)
            .setPositiveButton("OK", (d, w) -> answerIme(true, field.getText().toString()))
            .setNegativeButton(Strings.get("cancel"), (d, w) -> answerIme(false, ""))
            .setOnCancelListener(d -> answerIme(false, "")).create();
        field.setOnEditorActionListener((v, action, event) -> {
            if (action != EditorInfo.IME_ACTION_DONE) return false;
            dialog.dismiss(); answerIme(true, field.getText().toString()); return true;
        });
        dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
        dialog.setOnDismissListener(d -> { imeOpen = false; immersive(); surface.requestFocus(); });
        dialog.show(); field.requestFocus();
    }
    private void answerIme(boolean ok, String value) {
        if (!imeAnswered.compareAndSet(false, true)) return;
        File temporary = new File(BASE, "ime.request.result.tmp"), result = new File(BASE, "ime.request.result");
        try (java.io.Writer w = new java.io.OutputStreamWriter(new java.io.FileOutputStream(temporary), "UTF-8")) {
            w.write((ok ? "ok" : "cancel") + "\n" + value.replace('\n', ' ') + "\n");
        } catch (Exception ex) { android.util.Log.e("Uncharted", "text answer: " + ex); return; }
        if (!temporary.renameTo(result)) android.util.Log.e("Uncharted", "text answer: rename failed");
    }

    // ---------------------------------------------------------------------------------------------
    // The game folder (the dump with eboot.bin and big2/big3-ps4_Shipping.elf) in shared storage
    private static boolean isGame(File dir) { return dir != null && new File(dir, "eboot.bin").isFile(); }
    private boolean storageGranted() {
        return Build.VERSION.SDK_INT < 23 || checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE)
            == android.content.pm.PackageManager.PERMISSION_GRANTED;
    }
    private void requestStorage() {
        requestPermissions(new String[]{android.Manifest.permission.READ_EXTERNAL_STORAGE,
            android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQUEST_STORAGE);
    }
    private void askGameFolder() {
        if (!storageGranted()) { requestStorage(); return; }
        AlertDialog d = new AlertDialog.Builder(this).setTitle(Strings.get("folder_title"))
            .setMessage(Strings.get("folder_message") + (gameDir != null ? "\n\n" + gameDir : ""))
            .setPositiveButton(Strings.get("choose_folder"), (dialog, w) -> {
                try { startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_FOLDER); }
                catch (Exception ex) { toast("No folder picker: " + ex); }
            })
            .setNegativeButton(Strings.get("cancel"), null).create();
        show(d);
    }
    static File folderOf(android.net.Uri tree) {
        try {
            String id = android.provider.DocumentsContract.getTreeDocumentId(tree);
            int colon = id.indexOf(':');
            String volume = colon < 0 ? id : id.substring(0, colon), path = colon < 0 ? "" : id.substring(colon + 1);
            return DocPath.volume(android.os.Environment.getExternalStorageDirectory(), volume, path);
        } catch (Exception ex) { return null; }
    }
    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        if (code != REQUEST_STORAGE) return;
        if (results.length == 0 || results[0] != android.content.pm.PackageManager.PERMISSION_GRANTED)
            notice(Strings.get("storage_needed"));
    }
    @Override protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        immersive();
        if (code == REQUEST_LSFG) {
            if (result == RESULT_OK && data != null && data.getData() != null) installLsfgDll(data.getData());
            return;
        }
        if (code == REQUEST_DRIVER) {
            if (result == RESULT_OK && data != null && data.getData() != null) installDriver(data.getData());
            return;
        }
        if (code != REQUEST_FOLDER || result != RESULT_OK || data == null || data.getData() == null) return;
        File dir = folderOf(data.getData());
        if (dir != null && !isGame(dir)) {
            File[] children = dir.listFiles();
            if (children != null) for (File child : children) if (child.getName().startsWith("CUSA") && isGame(child)) { dir = child; break; }
        }
        if (!isGame(dir)) {
            AlertDialog d = new AlertDialog.Builder(this).setMessage((dir == null ? "" : dir.getPath() + ": ") + Strings.get("not_a_game"))
                .setPositiveButton(Strings.get("choose_again"), (dialog, w) -> askGameFolder())
                .setNegativeButton(Strings.get("cancel"), null).create();
            show(d);
            return;
        }
        gameDir = dir.getPath();
        getSharedPreferences("uncharted", MODE_PRIVATE).edit().putString("game_dir", gameDir).apply();
        setRowHint("folder", gameDir);
    }

    // ---------------------------------------------------------------------------------------------
    // LSFG: the player's own Lossless.dll, copied into the app (the game cannot read the picker's file)
    private File lsfgDll() { return new File(BASE, "lsfg/Lossless.dll"); }
    private String lsfgSummary() {
        File dll = lsfgDll();
        return dll.isFile() ? Strings.get("lsfg_dll_chosen") : Strings.get("lsfg_dll_hint");
    }
    private void askLsfgDll() {
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
        try { startActivityForResult(pick, REQUEST_LSFG); } catch (Exception ex) { toast("No file picker: " + ex); }
    }
    private void installLsfgDll(android.net.Uri uri) {
        Thread copy = new Thread(() -> {
            String problem = null;
            File dll = lsfgDll(), temporary = new File(dll.getPath() + ".tmp");
            dll.getParentFile().mkdirs();
            try (java.io.InputStream in = getContentResolver().openInputStream(uri);
                 java.io.OutputStream out = new java.io.FileOutputStream(temporary)) {
                if (in == null) throw new java.io.IOException("cannot open the file");
                byte[] buffer = new byte[1 << 16];
                byte[] head = new byte[2];
                long total = 0;
                for (int n; (n = in.read(buffer)) > 0; ) {
                    if (total == 0 && n >= 2) { head[0] = buffer[0]; head[1] = buffer[1]; }
                    out.write(buffer, 0, n); total += n;
                }
                if (head[0] != 'M' || head[1] != 'Z') throw new java.io.IOException(Strings.get("lsfg_not_dll"));
            } catch (Exception ex) { problem = ex.getMessage() != null ? ex.getMessage() : ex.toString(); }
            if (problem == null && !temporary.renameTo(dll)) problem = "cannot write " + dll;
            if (problem != null) temporary.delete();
            else bbport.set("lsfg_dll", dll.getPath());
            String failure = problem;
            main.post(() -> {
                if (destroyed) return;
                toast(failure == null ? Strings.get("lsfg_dll_chosen") : failure);
                setRowHint("lsfg", lsfgSummary());
            });
        }, "Lsfg-copy");
        copy.setDaemon(true); copy.start();
    }

    // ---------------------------------------------------------------------------------------------
    // The GPU driver: automatic (the bundled Turnip for this GPU), the phone's own, or one installed from a zip
    private String gpuSetting() {
        String value = launcher.get("gpu_driver", "auto").trim();
        if (value.equals("system")) return "system";
        if (value.isEmpty() || value.equals("auto") || drivers.find(value) == null) return "auto";
        return value;
    }
    private GpuFamily gpu() {
        GpuFamily g = gpu;
        if (g == null) {
            g = GpuFamily.detect(BASE);
            if (new File(BASE, "bionic/bin/kgsl-chip-id").exists()) gpu = g;
        }
        return g;
    }
    private String driverSummary() {
        String choice = gpuSetting();
        if (choice.equals("auto")) return Strings.get("driver_auto");
        if (choice.equals("system")) return Strings.get("driver_system");
        DriverManager.Driver d = drivers.find(choice);
        return d == null ? Strings.get("driver_auto") : d.title();
    }
    private void showDrivers() {
        Thread probe = new Thread(() -> { gpu(); main.post(() -> { if (!destroyed) showDriversDialog(); }); }, "Gpu-probe");
        probe.setDaemon(true); probe.start();
    }
    private static final DriverManager.Driver AUTO = new DriverManager.Driver("auto", "Automatic", "", "");
    private void showDriversDialog() {
        List<DriverManager.Driver> list = drivers.list();
        String setting = gpuSetting();
        DriverManager.Driver current = drivers.find(setting);
        CharSequence[] names = new CharSequence[list.size() + 2];
        names[0] = Strings.get("driver_auto") + " — " + AutoDriver.describe(gpu, Build.VERSION.SDK_INT);
        names[1] = Strings.get("driver_system");
        int checked = setting.equals("auto") ? 0 : current == null ? 1 : 0;
        for (int i = 0; i < list.size(); ++i) {
            names[i + 2] = list.get(i).title();
            if (current != null && current.id.equals(list.get(i).id)) checked = i + 2;
        }
        int[] choice = {checked};
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle(Strings.get("gpu_driver"))
            .setSingleChoiceItems(names, checked, (d, which) -> {
                choice[0] = which;
                ((AlertDialog) d).getButton(AlertDialog.BUTTON_NEGATIVE).setEnabled(which > 1);
            })
            .setPositiveButton(Strings.get("driver_use"), (d, w) -> useDriver(choice[0] == 0 ? AUTO : choice[0] == 1 ? null : list.get(choice[0] - 2)))
            .setNeutralButton(Strings.get("driver_add"), (d, w) -> askAddDriver())
            .setNegativeButton(Strings.get("driver_delete"), (d, w) -> { if (choice[0] > 1) deleteDriver(list.get(choice[0] - 2)); })
            .create();
        dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setEnabled(choice[0] > 1));
        show(dialog);
    }
    private void askAddDriver() {
        AlertDialog d = new AlertDialog.Builder(this).setTitle(Strings.get("driver_add"))
            .setItems(new String[]{Strings.get("driver_download"), Strings.get("driver_zip")}, (dialog, which) -> {
                if (which == 0) showDriverDownloads(); else askDriverZip();
            })
            .setNegativeButton(Strings.get("cancel"), null).create();
        show(d);
    }
    private void showDriverDownloads() {
        if (driverDownload != null) driverDownload.close();
        driverDownload = new DriverDownloadDialog(this, drivers, new File(FILES), new DriverDownloadDialog.Host() {
            @Override public String gpuVariant() { return gpu().family; }
            @Override public void use(DriverManager.Driver driver) { useDriver(driver); }
            @Override public void closed() { if (!destroyed) immersive(); }
            @Override public boolean destroyed() { return destroyed; }
        });
        driverDownload.show();
    }
    private void askDriverZip() {
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
            .putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"application/zip", "application/x-zip-compressed", "application/octet-stream"});
        try { startActivityForResult(pick, REQUEST_DRIVER); } catch (Exception ex) { toast("No file picker: " + ex); }
    }
    private void installDriver(android.net.Uri zip) {
        String zipName = "driver.zip";
        try (android.database.Cursor c = getContentResolver().query(zip,
                new String[]{android.provider.OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null) zipName = c.getString(0);
        } catch (Exception ignored) {}
        final String name = zipName;
        Thread install = new Thread(() -> {
            DriverManager.Driver installed = null; String failure = null;
            try (java.io.InputStream in = getContentResolver().openInputStream(zip)) {
                if (in == null) throw new java.io.IOException("Cannot open " + name);
                installed = drivers.install(in, name);
            } catch (Exception ex) { failure = ex.getMessage() != null ? ex.getMessage() : ex.toString(); }
            DriverManager.Driver result = installed; String problem = failure;
            main.post(() -> {
                if (destroyed) return;
                if (result == null) show(new AlertDialog.Builder(this).setTitle(Strings.get("driver_not_installed"))
                    .setMessage(problem).setPositiveButton("OK", null).create());
                else show(new AlertDialog.Builder(this).setMessage(Strings.format("driver_installed", result.title()))
                    .setNegativeButton(Strings.get("no"), null).setPositiveButton(Strings.get("driver_use"), (d, w) -> useDriver(result)).create());
            });
        }, "Driver-install");
        install.setDaemon(true); install.start();
    }
    private void useDriver(DriverManager.Driver driver) {
        String id = driver == null ? "system" : driver.id;
        if (id.equals(gpuSetting())) return;
        launcher.set("gpu_driver", id);
        setRowHint("driver", driverSummary());
        if (game != null) confirm(Strings.format("restart_driver", driverSummary()), this::restartGame);
    }
    private void deleteDriver(DriverManager.Driver driver) {
        boolean active = driver.id.equals(gpuSetting());
        if (active) launcher.set("gpu_driver", "auto");
        try { drivers.delete(driver.id); } catch (Exception ex) { toast(ex.getMessage()); }
        setRowHint("driver", driverSummary());
    }

    @Override protected void onDestroy() {
        destroyed = true;
        if (driverDownload != null) driverDownload.close();
        pad.reset(); audio.stop(); main.removeCallbacksAndMessages(null);
        stopGame();
        worker.shutdownNow();
        super.onDestroy();
    }
}
