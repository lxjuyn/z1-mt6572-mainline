#!/usr/bin/env python3
"""Bounded host regression test of the actual Home wallpaper methods with stubs.

No Android build or device operations. The extracted Java methods are unchanged;
framework stubs model successful reads, denied permission and absent wallpaper.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HOME = ROOT / 'android9/los16/development/samples/Home/src/com/example/android/home/Home.java'
JDK = ROOT / 'android9/los16/prebuilts/jdk/jdk8/linux-x86/bin'


def section(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 1
    cursor = opening + 1
    while depth:
        if source[cursor] == '{':
            depth += 1
        elif source[cursor] == '}':
            depth -= 1
        cursor += 1
    return source[start:cursor]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=HOME)
    args = parser.parse_args()
    source = args.source.read_text()
    methods = section(source, 'private void setDefaultWallpaper()')
    receiver = section(source, 'private class WallpaperIntentReceiver')
    java = r'''
import java.io.IOException;
public class WallpaperChecks {
    static class Drawable {}
    static class ClippedDrawable extends Drawable {
        ClippedDrawable(Drawable wallpaper) { if (wallpaper == null) throw new AssertionError("null wallpaper"); }
    }
    static class Window {
        Drawable background = new Drawable();
        int writes;
        void setBackgroundDrawable(Drawable d) { background=d; writes++; }
    }
    static class Context {}
    static class Intent {}
    static abstract class BroadcastReceiver { public abstract void onReceive(Context c, Intent i); }
    static class Log {
        static void e(String t, String m) {}
        static void w(String t, String m, Throwable e) {}
    }
    static final String LOG_TAG = "test";
    static boolean mWallpaperChecked;
    boolean denied, absent, clearDenied, clearIO;
    int reads, clears;
    final Window window = new Window();
    Window getWindow() { return window; }
    Drawable read() { reads++; if(denied) throw new SecurityException("READ_EXTERNAL_STORAGE"); return absent ? null : new Drawable(); }
    Drawable peekWallpaper() { return read(); }
    Drawable getWallpaper() { return read(); }
    void clearWallpaper() throws IOException {
        if(clearDenied) throw new SecurityException("SET_WALLPAPER");
        if(clearIO) throw new IOException("read error");
        clears++;
    }
    /*METHODS*/
    static WallpaperChecks fresh() { mWallpaperChecked=false; return new WallpaperChecks(); }
    static void check(boolean b, String message) { if(!b) throw new AssertionError(message); }
    public static void main(String[] args) {
        WallpaperChecks x=fresh(); x.denied=true; Drawable previous=x.window.background;
        x.setDefaultWallpaper();
        check(x.window.background==previous && x.window.writes==0 && x.clears==0, "onCreate denial changes wallpaper");
        x.setDefaultWallpaper(); check(x.reads==1, "checked wallpaper repeatedly read");
        x=new WallpaperChecks(); x.denied=true; previous=x.window.background;
        x.new WallpaperIntentReceiver().onReceive(new Context(),new Intent());
        check(x.window.background==previous && x.window.writes==0 && x.clears==0, "broadcast denial changes wallpaper");
        x=fresh(); x.setDefaultWallpaper(); check(x.window.writes==1 && x.clears==0, "granted startup read lost");
        x=new WallpaperChecks(); x.new WallpaperIntentReceiver().onReceive(new Context(),new Intent());
        check(x.window.writes==1, "granted broadcast read lost");
        x=fresh(); x.absent=true; x.setDefaultWallpaper(); check(x.clears==1, "default fallback changed");
        x=new WallpaperChecks(); x.absent=true; previous=x.window.background;
        x.new WallpaperIntentReceiver().onReceive(new Context(),new Intent());
        check(x.window.background==previous && x.window.writes==0, "null broadcast wallpaper changes background");
        x=fresh(); x.absent=true; x.clearIO=true; x.setDefaultWallpaper();
        check(x.clears==0 && mWallpaperChecked, "IOException not tolerated");
        x=fresh(); x.absent=true; x.clearDenied=true; x.setDefaultWallpaper();
        check(x.clears==0 && mWallpaperChecked, "clear permission denial not tolerated");
        System.out.println("PASS: 9 wallpaper regression checks");
    }
}
'''.replace('/*METHODS*/', methods + '\n' + receiver)
    with tempfile.TemporaryDirectory(prefix='z1-home-test-') as work:
        test = Path(work) / 'WallpaperChecks.java'
        test.write_text(java)
        subprocess.run([str(JDK / 'javac'), str(test)], check=True, timeout=60)
        subprocess.run([str(JDK / 'java'), '-cp', work, 'WallpaperChecks'], check=True, timeout=20)


if __name__ == '__main__':
    main()
