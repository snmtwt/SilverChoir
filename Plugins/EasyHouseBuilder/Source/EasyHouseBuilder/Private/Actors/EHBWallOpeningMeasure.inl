bool FEHBWallOpeningMeasure::RevealArea(const FEHBWallOpeningMeasureDomain& D,
 const TArray<TArray<FVector2d>>& Loops,double& OutArea,FName& Status)
{
 OutArea=0;
 for(double V:{D.StartLeft,D.StartRight,D.EndLeft,D.EndRight,D.Height,D.Thickness})
  if(!FMath::IsFinite(V)){Status=TEXT("InvalidRevealMeasureDomain");return false;}
 if(D.Height<=0||D.Thickness<=0||D.EndLeft<=D.StartLeft||D.EndRight<=D.StartRight)
 {Status=TEXT("InvalidRevealMeasureDomain");return false;}
 for(const auto& Loop:Loops)
 {
  if(Loop.Num()<3){Status=TEXT("InvalidRevealMeasureLoop");return false;}
  for(const auto& P:Loop)if(!FMath::IsFinite(P.X)||!FMath::IsFinite(P.Y)){Status=TEXT("InvalidRevealMeasureLoop");return false;}
 }
 double Total=0;
 struct FLine{double Slope=0,Offset=0;double At(double Y)const{return Slope*Y+Offset;}};
 for(const auto& Loop:Loops)for(int32 I=0;I<Loop.Num();++I)
 {
  const auto A=Loop[I],Delta=Loop[(I+1)%Loop.Num()]-A;const double Length=Delta.Length();if(Length<=1.e-12)continue;
  TArray<FLine> Lower{{0,0}},Upper{{0,1}};double YMin=-D.Thickness*0.5,YMax=D.Thickness*0.5;bool Empty=false,Exit=false;
  // Each wall halfspace is a*t + b*y + c >= 0, with edge parameter t in [0,1].
  auto Constrain=[&](double a,double b,double c)
  {
   if(a>1.e-12||a<-1.e-12){(a>0?Lower:Upper).Add({-b/a,-c/a});return;}
   if(b>1.e-12||b<-1.e-12){const double Y=-c/b;if(b>0)YMin=FMath::Max(YMin,Y);else YMax=FMath::Min(YMax,Y);return;}
   if(FMath::Abs(c)<=1.e-9)Exit=true;else if(c<0)Empty=true;
  };
  Constrain(Delta.Y,0,A.Y);Constrain(-Delta.Y,0,D.Height-A.Y);
  Constrain(Delta.X,-(D.StartLeft-D.StartRight)/D.Thickness,A.X-(D.StartLeft+D.StartRight)*0.5);
  Constrain(-Delta.X,(D.EndLeft-D.EndRight)/D.Thickness,(D.EndLeft+D.EndRight)*0.5-A.X);
  if(Empty||Exit||YMax<=YMin)continue;
  TArray<FLine> Lines=Lower;Lines.Append(Upper);TArray<double> Breaks{YMin,YMax};
  // Every envelope switch and every zero-width crossing partitions an interval
  // over which the admissible parameter width is exactly linear.
  for(int32 J=0;J<Lines.Num();++J)for(int32 K=J+1;K<Lines.Num();++K)
  {
   const double S=Lines[J].Slope-Lines[K].Slope;if(FMath::Abs(S)<=1.e-12)continue;
   const double Y=(Lines[K].Offset-Lines[J].Offset)/S;if(Y>YMin&&Y<YMax)Breaks.Add(Y);
  }
  Breaks.Sort();
  for(int32 J=0;J+1<Breaks.Num();++J)
  {
   const double L=Breaks[J],R=Breaks[J+1],Mid=(L+R)*0.5;if(R<=L)continue;
   FLine Low=Lower[0],High=Upper[0];for(const auto& V:Lower)if(V.At(Mid)>Low.At(Mid))Low=V;for(const auto& V:Upper)if(V.At(Mid)<High.At(Mid))High=V;
   if(High.At(Mid)<=Low.At(Mid))continue;
   Total+=Length*(R-L)*0.5*(FMath::Max(0.0,High.At(L)-Low.At(L))+FMath::Max(0.0,High.At(R)-Low.At(R)));
  }
 }
 if(!FMath::IsFinite(Total)){Status=TEXT("RevealMeasureOverflow");return false;}
 OutArea=Total;Status=TEXT("Measured");return true;
}
