#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "H5UI_RuntimeView.h"
#include "H5UI_ScriptRuntime.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FH5UICanvas2DTest,"H5UIPlugin.Canvas.BitmapAndDOM",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FH5UICanvas2DTest::RunTest(const FString&)
{
    FH5UI_RuntimeView View([]{},[this](const FString& E){AddError(E);},[](const FH5UI_Event&){},[this](const FString& E){AddError(E);});
    if(!TestTrue(TEXT("Native canvas document loads"),View.LoadString(TEXT("<html><body><canvas id='c' width='32' height='16'></canvas></body></html>"),TEXT("coui://uiresources/"))))return false;
    auto Check=[&](const TCHAR* Name,const TCHAR* Script)
    {FString R,E;const bool Ok=View.ExecuteJavaScript(Script,R,E);TestTrue(Name,Ok&&R==TEXT("true"));if(!Ok)AddError(E);};
    Check(TEXT("Stable context identity and unsupported WebGL"),TEXT("globalThis.c=document.getElementById('c');globalThis.ctx=c.getContext('2d');c.getContext('2d')===ctx&&c.getContext('webgl')===null&&c.width===32&&c.height===16"));
    Check(TEXT("Retained bitmap supports partial transparent clear; alpha does not affect clearRect"),TEXT("ctx.fillStyle='#ff0000';ctx.fillRect(0,0,32,16);ctx.globalAlpha=0;ctx.clearRect(8,0,8,16);let a=ctx.getImageData(0,0,32,1).data;a[0]===255&&a[3]===255&&a[8*4+3]===0&&a[20*4]===255"));
    Check(TEXT("Save restore includes alpha, style and transform"),TEXT("ctx.globalAlpha=1;ctx.save();ctx.translate(20,0);ctx.fillStyle='#00ff00';ctx.globalAlpha=.5;ctx.fillRect(0,0,4,4);ctx.restore();ctx.fillRect(0,4,4,4);let p=ctx.getImageData(21,1,1,1).data;ctx.fillStyle==='#ff0000'&&ctx.globalAlpha===1&&p[0]>120&&p[1]>120"));
    Check(TEXT("Resize to the same width clears pixels, path and drawing state"),TEXT("ctx.lineWidth=7;ctx.save();c.width=c.width;ctx.restore();ctx.getImageData(0,0,1,1).data[3]===0&&ctx.lineWidth===1&&ctx.fillStyle==='#000000'"));
    Check(TEXT("Gradient and independently retained second canvas"),TEXT("let g=ctx.createLinearGradient(0,0,32,0);g.addColorStop(0,'#ff0000');g.addColorStop(1,'#0000ff');ctx.fillStyle=g;ctx.fillRect(0,0,32,16);let l=ctx.getImageData(1,1,1,1).data;let r=ctx.getImageData(30,1,1,1).data;let other=document.createElement('canvas');document.body.appendChild(other);other.getContext('2d').fillRect(0,0,2,2);l[0]>l[2]&&r[2]>r[0]&&c.getContext('2d')===ctx"));
    Check(TEXT("Antialiased continuous path, curves and DPR transform"),TEXT("ctx.clearRect(0,0,32,16);ctx.setTransform(2,0,0,2,0,0);ctx.beginPath();ctx.moveTo(1,4);ctx.bezierCurveTo(4,4,6,4,9,4);ctx.quadraticCurveTo(11,4,14,4);ctx.lineWidth=1;ctx.lineCap='round';ctx.strokeStyle='#00ffff';ctx.stroke();ctx.getImageData(12,8,1,1).data[3]>100"));
    Check(TEXT("Invalid styles do not corrupt context state"),TEXT("ctx.globalAlpha=NaN;ctx.lineWidth=-2;ctx.strokeStyle='not-a-color';ctx.globalAlpha===1&&ctx.lineWidth===1&&ctx.strokeStyle==='#00ffff'"));
    Check(TEXT("Oversized native bitmap rejected without modifying size"),TEXT("let caught=false;try{c.width=99999}catch(e){caught=true}caught&&c.width===32"));
    Check(TEXT("Browser rgba alpha remains visible and fractional"),TEXT("c.width=32;ctx.fillStyle='rgba(20,220,240,0.5)';ctx.fillRect(0,0,4,4);let rgba=ctx.getImageData(1,1,1,1).data;rgba[1]===220&&rgba[3]>=127&&rgba[3]<=128"));
    TestFalse(TEXT("Canvas never loads browser module"),FModuleManager::Get().IsModuleLoaded(TEXT("H5UIPluginCEF")));
    View.Close();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FH5UICanvasAnimationTest,"H5UIPlugin.Canvas.AnimationFrames",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FH5UICanvasAnimationTest::RunTest(const FString&)
{
    FH5UI_RuntimeView View([]{},[this](const FString& E){AddError(E);},[](const FH5UI_Event&){},[this](const FString& E){AddError(E);});
    if(!View.LoadString(TEXT("<html><body><canvas id='c'></canvas></body></html>"),TEXT("coui://uiresources/")))return false;
    FString R,E;View.ExecuteJavaScript(TEXT("globalThis.frames=0;globalThis.times=[];function frame(t){frames++;times.push(t);requestAnimationFrame(frame);}requestAnimationFrame(frame);"),R,E);
    const double Now=FPlatformTime::Seconds()+2;
    for(int I=0;I<4;++I)View.Update({320,96},Now+I*.1);
    View.ExecuteJavaScript(TEXT("frames===4&&times[3]>times[0]"),R,E);TestEqual(TEXT("Recursive rAF runs once per update even with future clock"),R,FString(TEXT("true")));
    View.Close();TestTrue(TEXT("Animation page can reload without stale callbacks"),View.LoadString(TEXT("<html><body></body></html>"),TEXT("coui://uiresources/")));
    View.ExecuteJavaScript(TEXT("typeof frames==='undefined'"),R,E);TestEqual(TEXT("Previous runtime released"),R,FString(TEXT("true")));View.Close();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FH5UIECGPageTest,"H5UIPlugin.Canvas.ECGParameters",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FH5UIECGPageTest::RunTest(const FString&)
{
    FH5UI_RuntimeView View([]{},[this](const FString& E){AddError(E);},[](const FH5UI_Event&){},[this](const FString& E){AddError(E);});
    View.SetData(TEXT("ecgParameters"),TEXT("{\"color\":\"#ffbb22\",\"bpm\":120,\"intensity\":0.5}"));
    if(!TestTrue(TEXT("Packaged project ECG page loads natively"),View.LoadURL(TEXT("coui://uiresources/ECG/ecg.html"))))return false;
    View.Update({320,96},FPlatformTime::Seconds()+.1);
    FString R,E;
    auto Check=[&](const TCHAR* Name,const TCHAR* Script){const bool Ok=View.ExecuteJavaScript(Script,R,E);TestTrue(Name,Ok&&R==TEXT("true"));if(!Ok)AddError(E);};
    Check(TEXT("Initial Blueprint data arrives before history and first render"),TEXT("ecgMonitor.stop();ecgMonitor.parameters.bpm===120&&ecgMonitor.parameters.intensity===0.5&&ecgMonitor.parameters.color==='#ffbb22'&&Math.max(...ecgMonitor.samples)<=0.51"));
    Check(TEXT("ECG draws a nontransparent bitmap"),TEXT("ecgMonitor.draw();ecgMonitor.ctx.getImageData(10,10,1,1).data[3]>200"));
    Check(TEXT("Known P-QRS-T pulse morphology"),TEXT("SilverChoirECG.waveform(.345)>.9&&SilverChoirECG.waveform(.387)<0&&Math.abs(SilverChoirECG.waveform(.9))<.001"));
    Check(TEXT("BPM controls phase rate independently of intensity"),TEXT("ecgMonitor.phase=0;ecgMonitor.advance(.25);Math.abs(ecgMonitor.phase-.5)<.0001"));
    View.DispatchHtmlEvent(TEXT("ECGParameters"),TEXT("{\"bpm\":60,\"intensity\":0,\"color\":\"#ef5544\"}"));
    Check(TEXT("Zero intensity immediately removes all historical peaks"),TEXT("ecgMonitor.parameters.color==='#ef5544'&&ecgMonitor.samples.every(v=>v===0)"));
    Check(TEXT("Death line remains flat while scan advances"),TEXT("let before=ecgMonitor.head;ecgMonitor.advance(.1);ecgMonitor.draw();ecgMonitor.samples.every(v=>v===0)&&before!==ecgMonitor.head"));
    View.Close();return true;
}
#endif
