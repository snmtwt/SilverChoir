namespace
{
 bool CheckWallBaseSurfaces(FAutomationTestBase& Test,AEHBBuildingActorBase* B,bool RemoveRenderer)
 {
  int32 Count=0;for(auto* E:B->QueryElements(FEHBElementQuery()))if(auto* W=Cast<AEHB_Wall>(E))
  {
   TArray<FEHBLogicalSurfaceDefinition> Values;FName Status;if(!Test.TestTrue(*FString::Printf(TEXT("Wall base query: %s"),*Status.ToString()),W->QueryLogicalBaseSurfaces(Values,Status))||Values.Num()!=2)return false;++Count;
   for(int32 Side=0;Side<2;++Side)
   {
    const auto* Mesh=Side==0?W->LeftWallMeshComponent.Get():W->RightWallMeshComponent.Get();if(!Mesh)return false;double Area=0;
    for(int32 Section=0;Section<Mesh->GetNumSections();++Section)if(const auto* Part=Mesh->GetProcMeshSection(Section))for(int32 I=0;I<Part->ProcIndexBuffer.Num();I+=3){const auto A=Part->ProcVertexBuffer[Part->ProcIndexBuffer[I]].Position,C=Part->ProcVertexBuffer[Part->ProcIndexBuffer[I+1]].Position,D=Part->ProcVertexBuffer[Part->ProcIndexBuffer[I+2]].Position;Area+=FVector::CrossProduct(C-A,D-A).Size()*0.5;}
    if(!Test.TestTrue(TEXT("Topology wall logical area matches actual side mesh"),FMath::Abs(Area-Values[Side].GetAreaCm2())<=0.05))return false;
    Test.TestEqual(TEXT("Wall side owns stable semantic ID"),Values[Side].SurfaceGuid,W->FindLogicalSurfaceIdentity(Values[Side].SourceName));
   }
   if(RemoveRenderer)
   {
    TArray<UEHBGeneratedMeshComponent*> Components;W->GetGeneratedMeshComponents(Components);for(auto* C:Components)C->DestroyComponent();W->LeftWallMeshComponent=nullptr;W->RightWallMeshComponent=nullptr;W->CapMeshComponent=nullptr;
    TArray<FEHBLogicalSurfaceDefinition> After;if(!Test.TestTrue(TEXT("Topology wall base is renderer-independent"),W->QueryLogicalBaseSurfaces(After,Status))||After.Num()!=2)return false;
    for(int32 Side=0;Side<2;++Side){Test.TestEqual(TEXT("Removed renderer preserves wall identity"),After[Side].SurfaceGuid,Values[Side].SurfaceGuid);Test.TestTrue(TEXT("Removed renderer preserves wall frame"),After[Side].PlaneToBuilding.Equals(Values[Side].PlaneToBuilding,1.e-6));Test.TestTrue(TEXT("Removed renderer preserves wall area"),FMath::Abs(After[Side].GetAreaCm2()-Values[Side].GetAreaCm2())<0.01);}
   }
  }
  return Test.TestTrue(TEXT("Real building has logical walls"),Count>0);
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBLogicalWallTopologyTest,"EHB.Topology.LogicalWallSurfaces",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBLogicalWallTopologyTest::RunTest(const FString& Parameters)
{
 ON_SCOPE_EXIT{GEditor->SelectNone(false,true,false);};for(int32 Mode=0;Mode<3;++Mode){FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;if(!CheckWallBaseSurfaces(*this,Setup.Building(),true))return false;}return true;
}
