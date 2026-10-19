package com.android.support;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import static android.view.ViewGroup.LayoutParams.WRAP_CONTENT;

/**
 * HUD Status Panel for GTA SA Android Mod Menu.
 * Displays real-time counts for:
 * - Team Players (Local Player + Allies / Gang)
 * - Enemy Players (Rival Gangs / Hostile Targets)
 * - Bot / NPC Count (AI Peds / Civilians / SA-MP Bots)
 * - Total Active Streamed Peds
 */
public class HudStatusPanel {

    private final Context mContext;
    private final WindowManager mWindowManager;
    private final boolean mIsOverlay;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final SharedPreferences mPrefs;

    // Root UI container & WindowManager params
    private FrameLayout mRootFrame;
    private LinearLayout mMainCard;
    private LinearLayout mStatsContainer;
    private LinearLayout mCompactContainer;
    private WindowManager.LayoutParams mParams;

    // Header views
    private TextView mTvTitle;
    private TextView mTvToggleIcon;
    private TextView mTvStatusIndicator;

    // Detailed stats views
    private TextView mTvTeamCount;
    private TextView mTvEnemyCount;
    private TextView mTvBotCount;
    private TextView mTvTotalCount;

    // Compact mode views
    private TextView mTvCompactText;

    // Rows for conditional visibility
    private LinearLayout mRowTeam;
    private LinearLayout mRowEnemy;
    private LinearLayout mRowBot;

    // States & Preferences
    private boolean mIsVisible = true;
    private boolean mIsMinimized = false;
    private boolean mIsCompact = false;
    private boolean mShowTeam = true;
    private boolean mShowBots = true;
    private int mOpacityPercent = 88;
    private int mTextSizeSp = 13;
    private boolean mIsAttached = false;

    // Touch dragging variables
    private float mInitialTouchX, mInitialTouchY;
    private int mInitialX, mInitialY;
    private boolean mIsMoving = false;

    // Theme Colors
    private static final int COLOR_BG = Color.parseColor("#EE0D1520");
    private static final int COLOR_BORDER = Color.parseColor("#00E5FF");
    private static final int COLOR_CARD_BG = Color.parseColor("#4416222F");
    private static final int COLOR_TEAM = Color.parseColor("#00E676");     // Vibrant Green
    private static final int COLOR_ENEMY = Color.parseColor("#FF5252");    // Vibrant Red
    private static final int COLOR_BOT = Color.parseColor("#40C4FF");      // Vibrant Cyan
    private static final int COLOR_TOTAL = Color.parseColor("#ECEFF1");    // Clean White/Silver
    private static final int COLOR_LABEL = Color.parseColor("#90A4AE");    // Muted Label
    private static final int COLOR_ACCENT = Color.parseColor("#FFD700");   // Gold

    public HudStatusPanel(Context context, WindowManager windowManager, boolean isOverlay) {
        mContext = context;
        mWindowManager = windowManager;
        mIsOverlay = isOverlay;
        mPrefs = context.getSharedPreferences("GTA_HUD_STATUS_PREFS", Context.MODE_PRIVATE);

        // Load saved positions
        int savedX = mPrefs.getInt("HUD_X", 20);
        int savedY = mPrefs.getInt("HUD_Y", 120);

        initViews();
        setupWindowParams(savedX, savedY);
        startPolling();
    }

    private void initViews() {
        mRootFrame = new FrameLayout(mContext);

        mMainCard = new LinearLayout(mContext);
        mMainCard.setOrientation(LinearLayout.VERTICAL);
        mMainCard.setPadding(dp(10), dp(8), dp(10), dp(8));
        updateCardBackground();

        // ----------------- Header Section -----------------
        LinearLayout header = new LinearLayout(mContext);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(0, 0, 0, dp(6));

        mTvStatusIndicator = new TextView(mContext);
        mTvStatusIndicator.setText("● ");
        mTvStatusIndicator.setTextColor(COLOR_TEAM);
        mTvStatusIndicator.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);

        mTvTitle = new TextView(mContext);
        mTvTitle.setText("HUD STATUS • GTA:SA");
        mTvTitle.setTextColor(Color.WHITE);
        mTvTitle.setTypeface(Typeface.DEFAULT_BOLD);
        mTvTitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, mTextSizeSp);
        LinearLayout.LayoutParams titleParams = new LinearLayout.LayoutParams(0, WRAP_CONTENT, 1.0f);
        mTvTitle.setLayoutParams(titleParams);

        mTvToggleIcon = new TextView(mContext);
        mTvToggleIcon.setText("▼");
        mTvToggleIcon.setTextColor(COLOR_LABEL);
        mTvToggleIcon.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        mTvToggleIcon.setPadding(dp(6), 0, 0, 0);

        header.addView(mTvStatusIndicator);
        header.addView(mTvTitle);
        header.addView(mTvToggleIcon);

        header.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                toggleMinimize();
            }
        });

        // ----------------- Detailed Stats Container -----------------
        mStatsContainer = new LinearLayout(mContext);
        mStatsContainer.setOrientation(LinearLayout.VERTICAL);

        mRowTeam = createStatRow("🟢 TEAM PLAYERS", COLOR_TEAM);
        mTvTeamCount = (TextView) mRowTeam.getChildAt(1);

        mRowEnemy = createStatRow("🔴 ENEMY PLAYERS", COLOR_ENEMY);
        mTvEnemyCount = (TextView) mRowEnemy.getChildAt(1);

        mRowBot = createStatRow("🤖 BOTS / NPCS", COLOR_BOT);
        mTvBotCount = (TextView) mRowBot.getChildAt(1);

        LinearLayout rowTotal = createStatRow("👥 TOTAL ENTITIES", COLOR_TOTAL);
        mTvTotalCount = (TextView) rowTotal.getChildAt(1);

        mStatsContainer.addView(mRowTeam);
        mStatsContainer.addView(mRowEnemy);
        mStatsContainer.addView(mRowBot);
        mStatsContainer.addView(rowTotal);

        // ----------------- Compact Mode Container -----------------
        mCompactContainer = new LinearLayout(mContext);
        mCompactContainer.setOrientation(LinearLayout.HORIZONTAL);
        mCompactContainer.setGravity(Gravity.CENTER);
        mCompactContainer.setVisibility(View.GONE);

        mTvCompactText = new TextView(mContext);
        mTvCompactText.setText("🟢 0  |  🔴 0  |  🤖 0  |  👥 0");
        mTvCompactText.setTextColor(Color.WHITE);
        mTvCompactText.setTypeface(Typeface.DEFAULT_BOLD);
        mTvCompactText.setTextSize(TypedValue.COMPLEX_UNIT_SP, mTextSizeSp);
        mCompactContainer.addView(mTvCompactText);

        mMainCard.addView(header);
        mMainCard.addView(mStatsContainer);
        mMainCard.addView(mCompactContainer);

        mRootFrame.addView(mMainCard);
        mRootFrame.setOnTouchListener(createTouchListener());
    }

    private LinearLayout createStatRow(String label, int valueColor) {
        LinearLayout row = new LinearLayout(mContext);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(4), dp(3), dp(4), dp(3));
        row.setBackground(createRowBackground());

        LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT);
        rowParams.setMargins(0, dp(2), 0, dp(2));
        row.setLayoutParams(rowParams);

        TextView tvLabel = new TextView(mContext);
        tvLabel.setText(label);
        tvLabel.setTextColor(COLOR_LABEL);
        tvLabel.setTextSize(TypedValue.COMPLEX_UNIT_SP, (float)(mTextSizeSp - 2));
        tvLabel.setTypeface(Typeface.DEFAULT_BOLD);
        LinearLayout.LayoutParams lpLabel = new LinearLayout.LayoutParams(dp(110), WRAP_CONTENT);
        tvLabel.setLayoutParams(lpLabel);

        TextView tvValue = new TextView(mContext);
        tvValue.setText("0");
        tvValue.setTextColor(valueColor);
        tvValue.setTextSize(TypedValue.COMPLEX_UNIT_SP, (float)(mTextSizeSp + 1));
        tvValue.setTypeface(Typeface.DEFAULT_BOLD);
        tvValue.setGravity(Gravity.RIGHT);
        LinearLayout.LayoutParams lpValue = new LinearLayout.LayoutParams(dp(40), WRAP_CONTENT);
        tvValue.setLayoutParams(lpValue);

        row.addView(tvLabel);
        row.addView(tvValue);

        return row;
    }

    private GradientDrawable createRowBackground() {
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(COLOR_CARD_BG);
        gd.setCornerRadius(dp(4));
        return gd;
    }

    private void updateCardBackground() {
        GradientDrawable gd = new GradientDrawable();
        int alpha = (int) (255 * (mOpacityPercent / 100.0f));
        int dynamicBg = Color.argb(alpha, 13, 21, 32);
        gd.setColor(dynamicBg);
        gd.setCornerRadius(dp(8));
        gd.setStroke(dp(1), COLOR_BORDER);
        mMainCard.setBackground(gd);
    }

    @SuppressLint("WrongConstant")
    private void setupWindowParams(int posX, int posY) {
        int windowType;
        if (mIsOverlay) {
            windowType = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O ? 2038 : 2002;
        } else {
            windowType = WindowManager.LayoutParams.TYPE_APPLICATION;
        }

        mParams = new WindowManager.LayoutParams(
                WRAP_CONTENT,
                WRAP_CONTENT,
                posX,
                posY,
                windowType,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                PixelFormat.TRANSPARENT
        );
        mParams.gravity = Gravity.TOP | Gravity.LEFT;

        try {
            mWindowManager.addView(mRootFrame, mParams);
            mIsAttached = true;
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private View.OnTouchListener createTouchListener() {
        return new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        mInitialX = mParams.x;
                        mInitialY = mParams.y;
                        mInitialTouchX = event.getRawX();
                        mInitialTouchY = event.getRawY();
                        mIsMoving = false;
                        return true;

                    case MotionEvent.ACTION_MOVE:
                        int deltaX = (int) (event.getRawX() - mInitialTouchX);
                        int deltaY = (int) (event.getRawY() - mInitialTouchY);

                        if (Math.abs(deltaX) > 8 || Math.abs(deltaY) > 8) {
                            mIsMoving = true;
                        }

                        mParams.x = mInitialX + deltaX;
                        mParams.y = mInitialY + deltaY;

                        if (mIsAttached) {
                            mWindowManager.updateViewLayout(mRootFrame, mParams);
                        }
                        return true;

                    case MotionEvent.ACTION_UP:
                        if (!mIsMoving) {
                            // Tap detected: toggle minimize
                            toggleMinimize();
                        } else {
                            // Save last position
                            mPrefs.edit().putInt("HUD_X", mParams.x).putInt("HUD_Y", mParams.y).apply();
                        }
                        return true;
                }
                return false;
            }
        };
    }

    public void toggleMinimize() {
        mIsMinimized = !mIsMinimized;
        if (mIsMinimized) {
            mStatsContainer.setVisibility(View.GONE);
            mCompactContainer.setVisibility(View.GONE);
            mTvToggleIcon.setText("▲");
        } else {
            if (mIsCompact) {
                mStatsContainer.setVisibility(View.GONE);
                mCompactContainer.setVisibility(View.VISIBLE);
            } else {
                mStatsContainer.setVisibility(View.VISIBLE);
                mCompactContainer.setVisibility(View.GONE);
            }
            mTvToggleIcon.setText("▼");
        }
    }

    // ----------------- Real-time Polling Loop -----------------
    private final Runnable mPollRunnable = new Runnable() {
        @Override
        public void run() {
            if (mIsAttached && mIsVisible) {
                updateData();
            }
            mHandler.postDelayed(this, 250); // Refresh every 250ms for smooth live updates
        }
    };

    private void startPolling() {
        mHandler.post(mPollRunnable);
    }

    private void updateData() {
        try {
            int[] data = Menu.GetHudStatusData();
            if (data != null && data.length >= 5) {
                int team = data[0];
                int enemy = data[1];
                int bot = data[2];
                int total = data[3];
                int isLibLoaded = data[4];
                int localHp = (data.length >= 6) ? data[5] : 100;

                if (isLibLoaded == 1) {
                    mTvStatusIndicator.setTextColor(COLOR_TEAM);
                    mTvStatusIndicator.setText("● ");
                } else {
                    mTvStatusIndicator.setTextColor(Color.YELLOW);
                    mTvStatusIndicator.setText("◌ ");
                }

                mTvTeamCount.setText(String.valueOf(team));
                mTvEnemyCount.setText(String.valueOf(enemy));
                mTvBotCount.setText(String.valueOf(bot));
                mTvTotalCount.setText(String.valueOf(total));

                // Update compact display
                mTvCompactText.setText("🟢 " + team + "  🔴 " + enemy + "  🤖 " + bot + "  👥 " + total);
            }
        } catch (UnsatisfiedLinkError | Exception ignored) {
        }
    }

    // ----------------- Public Control Methods -----------------
    public void setVisibility(boolean visible) {
        mIsVisible = visible;
        if (mRootFrame != null) {
            mRootFrame.setVisibility(visible ? View.VISIBLE : View.GONE);
        }
    }

    public void setCompactMode(boolean compact) {
        mIsCompact = compact;
        if (!mIsMinimized) {
            mStatsContainer.setVisibility(compact ? View.GONE : View.VISIBLE);
            mCompactContainer.setVisibility(compact ? View.VISIBLE : View.GONE);
        }
    }

    public void setShowTeam(boolean show) {
        mShowTeam = show;
        if (mRowTeam != null) mRowTeam.setVisibility(show ? View.VISIBLE : View.GONE);
        if (mRowEnemy != null) mRowEnemy.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    public void setShowBots(boolean show) {
        mShowBots = show;
        if (mRowBot != null) mRowBot.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    public void setOpacity(int opacityPercent) {
        mOpacityPercent = Math.max(10, Math.min(100, opacityPercent));
        updateCardBackground();
    }

    public void setTextSize(int sizeSp) {
        mTextSizeSp = Math.max(8, Math.min(26, sizeSp));
        if (mTvTitle != null) mTvTitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, mTextSizeSp);
        if (mTvCompactText != null) mTvCompactText.setTextSize(TypedValue.COMPLEX_UNIT_SP, mTextSizeSp);
    }

    public void resetPosition() {
        if (mParams != null && mWindowManager != null && mIsAttached) {
            mParams.x = 20;
            mParams.y = 120;
            mWindowManager.updateViewLayout(mRootFrame, mParams);
            mPrefs.edit().putInt("HUD_X", 20).putInt("HUD_Y", 120).apply();
        }
    }

    public void destroy() {
        mHandler.removeCallbacks(mPollRunnable);
        if (mIsAttached && mRootFrame != null && mWindowManager != null) {
            try {
                mWindowManager.removeView(mRootFrame);
            } catch (Exception ignored) {
            }
            mIsAttached = false;
        }
    }

    private int dp(int value) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, value, mContext.getResources().getDisplayMetrics()
        );
    }
}
