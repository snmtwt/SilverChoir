#include "H5UI_CanvasScripting.h"
#include "H5UI_CanvasElement.h"

namespace
{
FString Str(JSContext* C,JSValueConst V)
{const char* S=JS_ToCString(C,V);if(!S)return {};FString R=UTF8_TO_TCHAR(S);JS_FreeCString(C,S);return R;}
double Number(JSContext* C,JSValueConst V)
{double N=0;return JS_ToFloat64(C,&N,V)<0?0:N;}
bool Brush(JSContext* C,JSValueConst V,FH5UI_CanvasBrush& B)
{
    if(JS_IsString(V))return FH5UI_CanvasElement::ParseColor(Str(C,V),B.Color);
    if(!JS_IsObject(V))return false;
    JSValue Coordinates=JS_GetPropertyStr(C,V,"points"),Stops=JS_GetPropertyStr(C,V,"stops");
    if(!JS_IsArray(Coordinates)||!JS_IsArray(Stops)){JS_FreeValue(C,Coordinates);JS_FreeValue(C,Stops);return false;}
    double N[4];for(int I=0;I<4;++I){auto X=JS_GetPropertyUint32(C,Coordinates,I);N[I]=Number(C,X);JS_FreeValue(C,X);}
    B.Start={N[0],N[1]};B.End={N[2],N[3]};
    auto Length=JS_GetPropertyStr(C,Stops,"length");const int Count=FMath::Clamp(int(Number(C,Length)),0,64);JS_FreeValue(C,Length);
    for(int I=0;I<Count;++I)
    {
        auto Stop=JS_GetPropertyUint32(C,Stops,I),Offset=JS_GetPropertyUint32(C,Stop,0),Color=JS_GetPropertyUint32(C,Stop,1);
        FLinearColor Parsed;if(FH5UI_CanvasElement::ParseColor(Str(C,Color),Parsed))B.Stops.Emplace(Number(C,Offset),Parsed);
        JS_FreeValue(C,Color);JS_FreeValue(C,Offset);JS_FreeValue(C,Stop);
    }
    JS_FreeValue(C,Coordinates);JS_FreeValue(C,Stops);B.Color=FLinearColor::Transparent;return true;
}
}
JSValue H5UICanvasCall(JSContext* C,FH5UI_CanvasElement& Canvas,int Count,JSValueConst* Args)
{
    if(Count<1)return JS_UNDEFINED;
    const FString Op=Str(C,Args[0]);
    auto N=[&](int I){return I<Count?Number(C,Args[I]):0.;};
    if(Op==TEXT("revision"))return JS_NewUint32(C,Canvas.Revision);
    if(Op==TEXT("color")){FLinearColor Color;return JS_NewBool(C,Count>1&&FH5UI_CanvasElement::ParseColor(Str(C,Args[1]),Color));}
    if(Op==TEXT("width")||Op==TEXT("height"))
    {
        const bool W=Op==TEXT("width");
        if(Count>1)
        {
            const double Value=N(1);if(!FMath::IsFinite(Value)||Value<0||Value>Canvas.MaxDimension||Value*(W?Canvas.Height:Canvas.Width)>Canvas.MaxPixels)
                return JS_ThrowRangeError(C,"Native canvas limit: dimension <= 2048, total pixels <= 1048576.");
            Canvas.SetAttribute(W?"width":"height",int(Value));
        }
        return JS_NewInt32(C,W?Canvas.Width:Canvas.Height);
    }
    if(Op==TEXT("begin")){Canvas.BeginPath();return JS_UNDEFINED;}
    if(Op==TEXT("close")){Canvas.ClosePath();return JS_UNDEFINED;}
    if(Op==TEXT("move")||Op==TEXT("line")||Op==TEXT("cubic")||Op==TEXT("quadratic"))
    {
        for(int I=1;I<Count;++I)if(!FMath::IsFinite(N(I))||FMath::Abs(N(I))>1.e6)return JS_UNDEFINED;
        if(Op==TEXT("move"))Canvas.MoveTo({N(1),N(2)});
        if(Op==TEXT("line"))Canvas.LineTo({N(1),N(2)});
        if(Op==TEXT("cubic"))Canvas.BezierTo({N(1),N(2)},{N(3),N(4)},{N(5),N(6)});
        if(Op==TEXT("quadratic"))Canvas.QuadraticTo({N(1),N(2)},{N(3),N(4)});
        return JS_UNDEFINED;
    }
    if(Op==TEXT("stroke")&&Count>=6)
    {
        FH5UI_CanvasBrush B;if(Brush(C,Args[3],B))Canvas.Stroke(B,float(N(1)),float(N(2)),Str(C,Args[4])==TEXT("round"),Str(C,Args[5])==TEXT("lighter"));
        return JS_UNDEFINED;
    }
    if((Op==TEXT("fillRect")||Op==TEXT("clearRect"))&&Count>=12)
    {
        TArray<FVector2D> Points;for(int I=1;I<=8;I+=2){if(!FMath::IsFinite(N(I))||!FMath::IsFinite(N(I+1))||FMath::Abs(N(I))>1.e6||FMath::Abs(N(I+1))>1.e6)return JS_UNDEFINED;Points.Add({N(I),N(I+1)});}
        FH5UI_CanvasBrush B;if(Brush(C,Args[10],B))Canvas.Rectangle(Points,B,float(N(9)),Op==TEXT("clearRect"),Str(C,Args[11])==TEXT("lighter"));
        return JS_UNDEFINED;
    }
    if(Op==TEXT("pixels")&&Count>=5)
    {
        const double X=N(1),Y=N(2),W=N(3),H=N(4);
        if(!FMath::IsFinite(X)||!FMath::IsFinite(Y)||FMath::Abs(X)>1.e6||FMath::Abs(Y)>1.e6||!FMath::IsFinite(W)||!FMath::IsFinite(H)||W<1||H<1||W>2048||H>2048||W*H>262144)
            return JS_ThrowRangeError(C,"getImageData requires a positive rectangle of at most 262144 pixels.");
        TArray<uint8> RGBA;RGBA.SetNumUninitialized(int(W)*int(H)*4);
        for(int I=0;I<int(W)*int(H);++I){const auto P=Canvas.ReadPixel(int(X)+I%int(W),int(Y)+I/int(W));RGBA[I*4]=P.R;RGBA[I*4+1]=P.G;RGBA[I*4+2]=P.B;RGBA[I*4+3]=P.A;}
        const JSValue Buffer=JS_NewArrayBufferCopy(C,RGBA.GetData(),RGBA.Num());
        return Buffer;
    }
    return JS_ThrowTypeError(C,"Unsupported native canvas operation.");
}

FString H5UICanvasBootstrap()
{
return UTF8_TO_TCHAR(R"JS(
(() => {
  const contexts = new WeakMap();
  const defaults = () => ({strokeStyle:'#000000',fillStyle:'#000000',lineWidth:1,globalAlpha:1,lineCap:'butt',lineJoin:'round',globalCompositeOperation:'source-over',m:[1,0,0,1,0,0]});
  class CanvasGradient {
    constructor(canvas, points) { this.canvas=canvas;this.points=points;this.stops=[]; }
    addColorStop(offset,color) {
      offset=Number(offset);color=String(color);
      if(!Number.isFinite(offset)||offset<0||offset>1)throw new RangeError('Color stop offset must be in [0,1].');
      if(!this.canvas.__canvas('color',color))throw new TypeError('Invalid canvas color.');
      if(this.stops.length>=64)throw new RangeError('Native gradient supports at most 64 stops.');
      this.stops.push([offset,color]);this.stops.sort((a,b)=>a[0]-b[0]);
    }
  }
  class CanvasRenderingContext2D {
    constructor(canvas) { this.canvas=canvas;this._revision=-1;this._sync(); }
    _sync() { const r=this.canvas.__canvas('revision');if(r!==this._revision){this._revision=r;this._s=defaults();this._stack=[];} }
    _point(x,y) { const m=this._s.m;return [m[0]*x+m[2]*y+m[4],m[1]*x+m[3]*y+m[5]]; }
    save() { this._sync();if(this._stack.length<128)this._stack.push({...this._s,m:this._s.m.slice()}); }
    restore() { this._sync();if(this._stack.length)this._s=this._stack.pop(); }
    beginPath() { this._sync();this.canvas.__canvas('begin'); }
    closePath() { this.canvas.__canvas('close'); }
    moveTo(x,y) { this._sync();this.canvas.__canvas('move',...this._point(+x,+y)); }
    lineTo(x,y) { this._sync();this.canvas.__canvas('line',...this._point(+x,+y)); }
    bezierCurveTo(x1,y1,x2,y2,x,y) { this._sync();this.canvas.__canvas('cubic',...this._point(+x1,+y1),...this._point(+x2,+y2),...this._point(+x,+y)); }
    quadraticCurveTo(x1,y1,x,y) { this._sync();this.canvas.__canvas('quadratic',...this._point(+x1,+y1),...this._point(+x,+y)); }
    rect(x,y,w,h) { this.moveTo(+x,+y);this.lineTo(+x+(+w),+y);this.lineTo(+x+(+w),+y+(+h));this.lineTo(+x,+y+(+h));this.closePath(); }
    arc(x,y,r,start,end,ccw=false) {
      x=+x;y=+y;r=+r;start=+start;end=+end;
      if(![x,y,r,start,end].every(Number.isFinite))return;
      if(r<0)throw new RangeError('Negative arc radius.');
      const tau=2*Math.PI;let sweep=end-start;
      if(!ccw){sweep=sweep>=tau?tau:((sweep%tau)+tau)%tau;}else{sweep=-sweep>=tau?-tau:-(((-sweep%tau)+tau)%tau);}
      const steps=Math.max(4,Math.min(256,Math.ceil(Math.abs(sweep)*r/2)));
      this.lineTo(x+Math.cos(start)*r,y+Math.sin(start)*r);
      for(let i=1;i<=steps;i++){const a=start+sweep*i/steps;this.lineTo(x+Math.cos(a)*r,y+Math.sin(a)*r);}
    }
    stroke() {
      this._sync();const s=this._s,m=s.m;const scale=Math.sqrt((m[0]*m[0]+m[1]*m[1]+m[2]*m[2]+m[3]*m[3])/2);
      this.canvas.__canvas('stroke',s.lineWidth*scale,s.globalAlpha,s.strokeStyle,s.lineCap,s.globalCompositeOperation);
    }
    _rect(op,x,y,w,h) { this._sync();const s=this._s;this.canvas.__canvas(op,...this._point(+x,+y),...this._point(+x+(+w),+y),...this._point(+x+(+w),+y+(+h)),...this._point(+x,+y+(+h)),s.globalAlpha,s.fillStyle,s.globalCompositeOperation); }
    clearRect(x,y,w,h) { this._rect('clearRect',x,y,w,h); }
    fillRect(x,y,w,h) { this._rect('fillRect',x,y,w,h); }
    setTransform(a,b,c,d,e,f) { this._sync();const m=[a,b,c,d,e,f].map(Number);if(m.every(Number.isFinite))this._s.m=m; }
    resetTransform() { this.setTransform(1,0,0,1,0,0); }
    transform(a,b,c,d,e,f) { this._sync();const m=this._s.m;this.setTransform(m[0]*a+m[2]*b,m[1]*a+m[3]*b,m[0]*c+m[2]*d,m[1]*c+m[3]*d,m[0]*e+m[2]*f+m[4],m[1]*e+m[3]*f+m[5]); }
    scale(x,y) { this.transform(+x,0,0,+y,0,0); }
    translate(x,y) { this.transform(1,0,0,1,+x,+y); }
    rotate(a) { this.transform(Math.cos(+a),Math.sin(+a),-Math.sin(+a),Math.cos(+a),0,0); }
    createLinearGradient(x0,y0,x1,y1) { this._sync();const p=[...this._point(+x0,+y0),...this._point(+x1,+y1)];if(!p.every(Number.isFinite))throw new TypeError('Invalid gradient coordinates.');return new CanvasGradient(this.canvas,p); }
    getImageData(x,y,w,h) { w=Math.trunc(+w);h=Math.trunc(+h);return {width:w,height:h,data:new Uint8ClampedArray(this.canvas.__canvas('pixels',+x,+y,w,h))}; }
  }
  for(const name of ['strokeStyle','fillStyle','lineWidth','globalAlpha','lineCap','lineJoin','globalCompositeOperation']) {
    Object.defineProperty(CanvasRenderingContext2D.prototype,name,{get(){this._sync();return this._s[name];},set(v){
      this._sync();
      if(name==='strokeStyle'||name==='fillStyle'){if(!(v instanceof CanvasGradient)&&!this.canvas.__canvas('color',String(v)))return;}
      else if(name==='lineWidth'){v=+v;if(!Number.isFinite(v)||v<=0)return;}
      else if(name==='globalAlpha'){v=+v;if(!Number.isFinite(v)||v<0||v>1)return;}
      else if(name==='lineCap'){if(v!=='butt'&&v!=='round')return;}
      else if(name==='lineJoin'){if(v!=='round')return;}
      else if(name==='globalCompositeOperation'){if(v!=='source-over'&&v!=='lighter')return;}
      this._s[name]=v;
    }});
  }
  Element.prototype.getContext=function(kind){if(this.tagName.toLowerCase()!=='canvas'||kind!=='2d')return null;let ctx=contexts.get(this);if(!ctx){ctx=new CanvasRenderingContext2D(this);contexts.set(this,ctx);}return ctx;};
  for(const name of ['width','height'])Object.defineProperty(Element.prototype,name,{get(){return this.tagName.toLowerCase()==='canvas'?this.__canvas(name):Number(this.getAttribute(name)||0);},set(v){if(this.tagName.toLowerCase()==='canvas')this.__canvas(name,Math.trunc(Number(v)));else this.setAttribute(name,v);}});
  globalThis.CanvasRenderingContext2D=CanvasRenderingContext2D;globalThis.CanvasGradient=CanvasGradient;
})();
)JS");
}
