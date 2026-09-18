"""Exercise the actual UPL Java keyboard actions without requiring a worn headset.

Android widget/JNI boundaries are stubbed; chat packet tests live in ACEChatTests.
This checks the action wiring that a synthetic TextEntryAccepted test alone misses.
"""
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
plugin = root / "Unreal/Plugins/ACEClient/Source/ACEClient/ACEClient_Android.xml"
xml = ET.parse(plugin).getroot()
on_create = xml.find("gameActivityOnCreateAdditions/insert").text
methods = xml.find("gameActivityClassAdditions/insert").text.split("// Keep Quest's IME")[0]
out = root / "Unreal/Saved/Automation/KeyboardSubmitJava"
out.mkdir(parents=True, exist_ok=True)
java = r'''
public class KeyboardSubmitTest {
    interface OnEditorActionListener { boolean onEditorAction(TextView v, int action, KeyEvent event); }
    static class EditorInfo { static final int IME_MASK_ACTION=255, IME_ACTION_DONE=6, IME_ACTION_SEND=4, IME_ACTION_GO=2; }
    static class InputType { static final int TYPE_TEXT_FLAG_MULTI_LINE=131072; }
    static class KeyEvent {
        static final int KEYCODE_ENTER=66, ACTION_DOWN=0, ACTION_UP=1;
        final int action; KeyEvent(int value) { action=value; }
        int getKeyCode() { return KEYCODE_ENTER; } int getAction() { return action; }
    }
    static class TextView {
        String text=""; int inputType; OnEditorActionListener listener;
        void setOnEditorActionListener(OnEditorActionListener value) { listener=value; }
        int getInputType() { return inputType; } String getText() { return text; }
    }
    static class Handler { int removed; void removeCallbacks(Runnable task) { removed++; } }
    boolean bUsesVrKeyboard, bKeyboardInputCommittedByEnterKey;
    TextView newVirtualKeyboardInput=new TextView();
    Handler virtualKeyboardHandler=new Handler(); Runnable virtualKeyboardTextChangeRunnable;
    int accepted, hidden; String acceptedText;
    void nativeVirtualKeyboardResult(boolean accept, String text) { if (accept) { accepted++; acceptedText=text; } }
    void AndroidThunkJava_HideVirtualKeyboardInput() { hidden++; }
    void runOnUiThread(Runnable task) { task.run(); }
    KeyboardSubmitTest() { ON_CREATE }
    METHODS
    static void check(boolean result, String name) { if (!result) throw new AssertionError(name); }
    boolean action(int id, KeyEvent event) { return newVirtualKeyboardInput.listener.onEditorAction(newVirtualKeyboardInput,id,event); }
    public static void main(String[] args) {
        for (int action : new int[]{EditorInfo.IME_ACTION_DONE,EditorInfo.IME_ACTION_SEND,EditorInfo.IME_ACTION_GO}) {
            KeyboardSubmitTest t=new KeyboardSubmitTest();
            t.newVirtualKeyboardInput.text="final character!";
            t.virtualKeyboardTextChangeRunnable=()->{ throw new AssertionError("stale text timer ran"); };
            check(t.action(action,null),"confirmation consumed");
            check(t.accepted==1 && t.hidden==1 && t.acceptedText.equals("final character!"),"confirmation accepts current buffer and hides");
            check(t.virtualKeyboardTextChangeRunnable==null && t.virtualKeyboardHandler.removed==1,"pending edits cleared");
            t.action(action,null); check(t.accepted==1,"duplicate action suppressed");
        }
        KeyboardSubmitTest keys=new KeyboardSubmitTest(); keys.newVirtualKeyboardInput.text="Enter final";
        keys.action(0,new KeyEvent(KeyEvent.ACTION_DOWN)); keys.action(0,new KeyEvent(KeyEvent.ACTION_UP));
        check(keys.accepted==1 && keys.acceptedText.equals("Enter final"),"single-line key action submits once");
        KeyboardSubmitTest multiline=new KeyboardSubmitTest(); multiline.newVirtualKeyboardInput.inputType=InputType.TYPE_TEXT_FLAG_MULTI_LINE;
        check(!multiline.action(0,new KeyEvent(KeyEvent.ACTION_DOWN)) && multiline.accepted==0,"multiline Enter is left alone");
        KeyboardSubmitTest untouched=new KeyboardSubmitTest();
        check(!untouched.action(0,null) && untouched.accepted==0,"non-confirming action cannot submit");
        KeyboardSubmitTest raw=new KeyboardSubmitTest();
        raw.bKeyboardInputCommittedByEnterKey=true; // UE raw-key path has already scheduled Hide.
        raw.newVirtualKeyboardInput.text="last raw-key edit";
        raw.AndroidThunkJava_ACEAcceptKeyboardInput();
        check(raw.accepted==1 && raw.hidden==0 && raw.acceptedText.equals("last raw-key edit"),"raw Enter supplies final text despite prior Hide request");
        KeyboardSubmitTest hardware=new KeyboardSubmitTest();
        hardware.AndroidThunkJava_ACEAcceptKeyboardInput();
        check(hardware.accepted==1 && hardware.hidden==1,"physical Enter also hides and completes");
        System.out.println("PASS: 8 native keyboard action scenarios (actual UPL Java; stub Android/JNI boundaries)");
    }
}
'''.replace("ON_CREATE", on_create).replace("METHODS", methods)
source = out / "KeyboardSubmitTest.java"
source.write_text(java, encoding="utf-8")
jdk = Path("C:/Program Files/Android/openjdk/jdk-21.0.8/bin")
subprocess.run([str(jdk / "javac.exe"), "-d", str(out), str(source)], check=True)
subprocess.run([str(jdk / "java.exe"), "-cp", str(out), "KeyboardSubmitTest"], check=True)
