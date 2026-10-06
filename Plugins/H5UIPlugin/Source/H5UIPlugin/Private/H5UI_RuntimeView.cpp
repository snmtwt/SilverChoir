#include "H5UI_RuntimeView.h"

#include "RmlUi/Core.h"
#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/ElementText.h"
#include "RmlUi/Core/ElementUtilities.h"
#include "RmlUi/Core/FontEngineInterface.h"
#include "RmlUi/Core/FontMetrics.h"
#include "RmlUi/Core/Elements/ElementFormControl.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"
#include "RmlUi/Core/StringUtilities.h"
#include "H5UI_Input.h"
#include "H5UI_Interfaces.h"
#include "H5UI_ScriptRuntime.h"
#include "H5UI_Module.h"
#include "H5UI_Settings.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(H5UIBindings, true);

DEFINE_LOG_CATEGORY_STATIC(LogH5UIStrategyControlLayout, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogH5UIPointerRepaintTrace, Log, All);

namespace
{
	int32 GH5UI_ContextSerial = 0;
	const TCHAR* HtmlDisplayDefaults =
		TEXT("<style>\n")
		// Browsers always provide a default sans-serif face. RmlUi does not, so
		// documents without an explicit local font declaration otherwise emit one
		// missing-font warning for every text node during editor validation.
		TEXT("rml, body { font-family: Roboto; }\n")
		TEXT("address, article, aside, blockquote, body, dd, details, div, dl, dt, fieldset, figcaption, figure, footer, form, h1, h2, h3, h4, h5, h6, header, hgroup, hr, iframe, li, main, nav, ol, p, pre, section, summary, ul { display: block; }\n")
		TEXT("table { display: table; }\n")
		TEXT("thead, tbody, tfoot { display: table-row-group; }\n")
		TEXT("tr { display: table-row; }\n")
		TEXT("colgroup { display: table-column-group; }\n")
		TEXT("col { display: table-column; }\n")
		TEXT("td, th { display: table-cell; }\n")
		TEXT("font { display: inline; }\n")
		TEXT("pre { white-space: pre; font-family: \"Droid Sans Mono\"; }\n")
		TEXT("code, kbd, samp { display: inline; font-family: \"Droid Sans Mono\"; }\n")
		TEXT("sub { font-size: 0.75em; vertical-align: sub; }\n")
		TEXT("sup { font-size: 0.75em; vertical-align: super; }\n")
		TEXT("button { display: flex; align-items: center; justify-content: center; text-align: center; }\n")
		TEXT("scrollbarvertical { width: 12px; background-color: #111820; }\n")
		TEXT("scrollbarhorizontal { height: 12px; background-color: #111820; }\n")
		TEXT("scrollbarcorner { width: 12px; height: 12px; background-color: #111820; }\n")
		TEXT("scrollbarvertical sliderbar { min-width: 8px; min-height: 24px; margin-left: 2px; background-color: #526579; border-radius: 4px; }\n")
		TEXT("scrollbarhorizontal sliderbar { min-width: 24px; min-height: 8px; margin-top: 2px; background-color: #526579; border-radius: 4px; }\n")
		TEXT("scrollbarvertical sliderbar:hover, scrollbarhorizontal sliderbar:hover { background-color: #71869d; }\n")
		TEXT("input[type=checkbox], input[type=radio] { width: 16px; height: 16px; padding: 0; box-sizing: border-box; background-color: #0f151c; border: 2px solid #526579; }\n")
		TEXT("input[type=checkbox] { border-radius: 3px; }\n")
		TEXT("input[type=radio] { border-radius: 8px; }\n")
		TEXT("input[type=checkbox]:hover, input[type=radio]:hover { border-color: #7be0bc; }\n")
		TEXT("input[type=checkbox]:checked, input[type=radio]:checked { background-color: #5ed1a8; border-color: #d8fff1; }\n")
		TEXT("input[type=text], input[type=password], input[type=search], input[type=email], input[type=url], input[type=tel], input[type=number] { padding-top: 10px; padding-bottom: 10px; line-height: 18px; }\n")
		TEXT("input[type=range] { padding: 0; background-color: transparent; border-width: 0; }\n")
		TEXT("input[type=range] slidertrack { height: 6px; margin-top: 8px; background-color: #344457; border-radius: 3px; }\n")
		TEXT("input[type=range] sliderprogress { height: 6px; background-color: #5ed1a8; border-radius: 3px; }\n")
		TEXT("input[type=range] sliderbar { width: 18px; height: 18px; margin-top: 2px; box-sizing: border-box; background-color: #eef8f4; border: 3px solid #5ed1a8; border-radius: 9px; }\n")
		TEXT("input[type=range] sliderbar:hover, input[type=range] sliderbar:active { background-color: #ffffff; border-color: #7be0bc; }\n")
		TEXT("input[type=range] sliderarrowdec, input[type=range] sliderarrowinc { width: 0; height: 0; }\n")
		TEXT("input[type=color] { position: relative; min-width: 40px; min-height: 32px; padding: 4px; box-sizing: border-box; cursor: pointer; }\n")
		TEXT("input[type=color] colorvalue { display: block; width: 100%; height: 100%; box-sizing: border-box; border: 1px solid #dce5ef; border-radius: 2px; }\n")
		TEXT("input[type=color] colorpicker { display: none; position: absolute; left: 0; top: 100%; width: 168px; padding: 6px; box-sizing: border-box; flex-wrap: wrap; gap: 4px; z-index: 1000; background-color: #111820; border: 1px solid #526579; border-radius: 4px; box-shadow: 0 8px 18px #000000; }\n")
		TEXT("input[type=color].open colorpicker { display: flex; }\n")
		TEXT("input[type=color] coloroption { display: block; width: 22px; height: 22px; box-sizing: border-box; border: 1px solid #dce5ef; border-radius: 3px; cursor: pointer; }\n")
		TEXT("input[type=color] coloroption:hover { border-color: #ffffff; }\n")
		TEXT("select selectvalue { display: block; width: auto; min-height: 38px; margin-right: 34px; padding: 10px 11px; box-sizing: border-box; overflow: hidden; white-space: nowrap; text-overflow: ellipsis; line-height: 18px; color: #eef3f8; }\n")
		TEXT("select selectarrow { width: 0; height: 0; margin: 16px 14px 0 0; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 6px solid #9eacbc; }\n")
		TEXT("select selectarrow:hover, select selectarrow:checked { border-top-color: #5ed1a8; }\n")
		TEXT("select { position: relative; }\n")
		TEXT("select selectbox { display: block; width: 100%; max-height: 240px; margin-top: 4px; box-sizing: border-box; background-color: #111820; border: 1px solid #526579; border-radius: 4px; }\n")
		TEXT("select selectbox option { display: block; min-height: 36px; padding: 9px 11px; box-sizing: border-box; color: #c7d2df; background-color: #111820; }\n")
		TEXT("select selectbox option:hover { color: #ffffff; background-color: #202c38; }\n")
		TEXT("select selectbox option:checked { color: #07140f; background-color: #5ed1a8; }\n")
		TEXT("progress { display: block; overflow: hidden; box-sizing: border-box; background-color: #0f151c; border: 1px solid #344457; border-radius: 4px; }\n")
		TEXT("progress fill { background-color: #5ed1a8; border-radius: 3px; }\n")
		TEXT("</style>\n");

	Rml::String ToRmlString(const FString& Value)
	{
		return Rml::String(TCHAR_TO_UTF8(*Value));
	}

	FString FromRmlString(const Rml::String& Value)
	{
		return UTF8_TO_TCHAR(Value.c_str());
	}

	bool IsTruthyModelValue(const FString& Value)
	{
		return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("on"), ESearchCase::IgnoreCase) ||
			Value == TEXT("1");
	}

	bool IsEditableInputType(const FString& InputType)
	{
		return InputType.IsEmpty() ||
			InputType == TEXT("text") ||
			InputType == TEXT("password") ||
			InputType == TEXT("search") ||
			InputType == TEXT("email") ||
			InputType == TEXT("url") ||
			InputType == TEXT("tel") ||
			InputType == TEXT("number");
	}

	Rml::Element* FindDescendantByTag(Rml::Element* Root, const Rml::String& TagName)
	{
		if (!Root)
		{
			return nullptr;
		}

		for (int32 ChildIndex = 0; ChildIndex < Root->GetNumChildren(true); ++ChildIndex)
		{
			Rml::Element* Child = Root->GetChild(ChildIndex);
			if (Child->GetTagName() == TagName)
			{
				return Child;
			}
			if (Rml::Element* Match = FindDescendantByTag(Child, TagName))
			{
				return Match;
			}
		}
		return nullptr;
	}

	void SelfCloseHtmlVoidElements(FString& Document);
	void EnsureStylesheetLinkTypes(FString& Document);

	void NormalizeInlineStylePseudoElements(
		FString& Document,
		TArray<FH5UI_GeneratedPseudoSelector>& GeneratedPseudoSelectors)
	{
		int32 SearchFrom = 0;
		while (SearchFrom < Document.Len())
		{
			const int32 StyleStart = Document.Find(TEXT("<style"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			if (StyleStart == INDEX_NONE)
			{
				break;
			}
			const int32 ContentStartTag = Document.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, StyleStart);
			const int32 StyleEnd = ContentStartTag == INDEX_NONE
				? INDEX_NONE
				: Document.Find(TEXT("</style>"), ESearchCase::IgnoreCase, ESearchDir::FromStart, ContentStartTag + 1);
			if (ContentStartTag == INDEX_NONE || StyleEnd == INDEX_NONE)
			{
				break;
			}

			const int32 ContentStart = ContentStartTag + 1;
			FString Css = Document.Mid(ContentStart, StyleEnd - ContentStart);
			H5UIPlugin::NormalizeCssForRml(Css, &GeneratedPseudoSelectors);
			Document = Document.Left(ContentStart) + Css + Document.Mid(StyleEnd);
			SearchFrom = ContentStart + Css.Len() + 8;
		}
	}

	void NormalizeHtmlDocumentForRml(
		FString& Document,
		TArray<FH5UI_GeneratedPseudoSelector>& GeneratedPseudoSelectors)
	{
		const bool bStandardHtml = Document.Find(TEXT("<html"), ESearchCase::IgnoreCase) != INDEX_NONE;
		if (!bStandardHtml)
		{
			return;
		}

		// Browser translation extensions commonly wrap text in <font> with this unsupported inline value.
		Document.ReplaceInline(
			TEXT("vertical-align: inherit"),
			TEXT("vertical-align: baseline"),
			ESearchCase::IgnoreCase);

		NormalizeInlineStylePseudoElements(Document, GeneratedPseudoSelectors);
		EnsureStylesheetLinkTypes(Document);

		// Shared CSS compat for style attributes and remaining browser spellings.
		H5UIPlugin::NormalizeCssForRml(Document);

		const int32 DoctypeStart = Document.Find(TEXT("<!doctype"), ESearchCase::IgnoreCase);
		if (DoctypeStart != INDEX_NONE)
		{
			const int32 DoctypeEnd = Document.Find(
				TEXT(">"),
				ESearchCase::CaseSensitive,
				ESearchDir::FromStart,
				DoctypeStart);
			if (DoctypeEnd != INDEX_NONE)
			{
				Document.RemoveAt(DoctypeStart, DoctypeEnd - DoctypeStart + 1);
			}
		}

		Document.ReplaceInline(TEXT("<html"), TEXT("<rml"), ESearchCase::IgnoreCase);
		Document.ReplaceInline(TEXT("</html>"), TEXT("</rml>"), ESearchCase::IgnoreCase);
		SelfCloseHtmlVoidElements(Document);

		const int32 HeadStart = Document.Find(TEXT("<head"), ESearchCase::IgnoreCase);
		const int32 HeadEnd = HeadStart == INDEX_NONE
			? INDEX_NONE
			: Document.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, HeadStart);
		if (HeadEnd != INDEX_NONE)
		{
			Document.InsertAt(HeadEnd + 1, HtmlDisplayDefaults);
		}
		else
		{
			const int32 RootStart = Document.Find(TEXT("<rml"), ESearchCase::IgnoreCase);
			const int32 RootEnd = RootStart == INDEX_NONE
				? INDEX_NONE
				: Document.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, RootStart);
			if (RootEnd != INDEX_NONE)
			{
				Document.InsertAt(RootEnd + 1, FString(TEXT("<head>")) + HtmlDisplayDefaults + TEXT("</head>"));
			}
		}
	}

	bool IsHtmlNameBoundary(TCHAR Character)
	{
		return FChar::IsWhitespace(Character) || Character == TEXT('>') || Character == TEXT('/');
	}

	int32 FindHtmlTagEnd(const FString& Document, int32 TagStart)
	{
		TCHAR Quote = 0;
		for (int32 Index = TagStart; Index < Document.Len(); ++Index)
		{
			const TCHAR Character = Document[Index];
			if (Quote != 0)
			{
				if (Character == Quote)
				{
					Quote = 0;
				}
				continue;
			}
			if (Character == TEXT('\'') || Character == TEXT('"'))
			{
				Quote = Character;
			}
			else if (Character == TEXT('>'))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool IsHtmlVoidElementName(const FString& TagName)
	{
		return TagName.Equals(TEXT("area"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("base"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("br"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("col"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("embed"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("hr"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("img"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("input"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("link"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("meta"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("param"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("source"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("track"), ESearchCase::IgnoreCase) ||
			TagName.Equals(TEXT("wbr"), ESearchCase::IgnoreCase);
	}

	void SelfCloseHtmlVoidElements(FString& Document)
	{
		int32 SearchFrom = 0;
		while (SearchFrom < Document.Len())
		{
			const int32 TagStart = Document.Find(TEXT("<"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (TagStart == INDEX_NONE || TagStart + 1 >= Document.Len())
			{
				break;
			}

			const TCHAR First = Document[TagStart + 1];
			if (First == TEXT('/') || First == TEXT('!') || First == TEXT('?'))
			{
				SearchFrom = TagStart + 2;
				continue;
			}

			int32 NameStart = TagStart + 1;
			while (NameStart < Document.Len() && FChar::IsWhitespace(Document[NameStart]))
			{
				++NameStart;
			}

			int32 NameEnd = NameStart;
			while (NameEnd < Document.Len() &&
				!FChar::IsWhitespace(Document[NameEnd]) &&
				Document[NameEnd] != TEXT('>') &&
				Document[NameEnd] != TEXT('/'))
			{
				++NameEnd;
			}
			if (NameEnd <= NameStart)
			{
				SearchFrom = TagStart + 1;
				continue;
			}

			const int32 TagEnd = FindHtmlTagEnd(Document, TagStart);
			if (TagEnd == INDEX_NONE)
			{
				break;
			}
			if (!IsHtmlVoidElementName(Document.Mid(NameStart, NameEnd - NameStart)))
			{
				SearchFrom = TagEnd + 1;
				continue;
			}

			int32 BeforeEnd = TagEnd - 1;
			while (BeforeEnd > TagStart && FChar::IsWhitespace(Document[BeforeEnd]))
			{
				--BeforeEnd;
			}
			if (BeforeEnd >= TagStart && Document[BeforeEnd] != TEXT('/'))
			{
				Document.InsertAt(TagEnd, TEXT("/"));
				SearchFrom = TagEnd + 2;
			}
			else
			{
				SearchFrom = TagEnd + 1;
			}
		}
	}

	FString ReadHtmlAttribute(const FString& StartTag, const FString& RequestedName)
	{
		int32 Index = StartTag.Find(TEXT("<"), ESearchCase::CaseSensitive);
		Index = Index == INDEX_NONE ? 0 : Index + 1;
		while (Index < StartTag.Len() && FChar::IsWhitespace(StartTag[Index]))
		{
			++Index;
		}
		// Skip the element name so the same attribute reader works for link,
		// script, and future compatibility rewrites.
		while (Index < StartTag.Len() &&
			!FChar::IsWhitespace(StartTag[Index]) &&
			StartTag[Index] != TEXT('>') &&
			StartTag[Index] != TEXT('/'))
		{
			++Index;
		}
		while (Index < StartTag.Len())
		{
			while (Index < StartTag.Len() && (FChar::IsWhitespace(StartTag[Index]) || StartTag[Index] == TEXT('/')))
			{
				++Index;
			}
			const int32 NameStart = Index;
			while (Index < StartTag.Len() &&
				!FChar::IsWhitespace(StartTag[Index]) &&
				StartTag[Index] != TEXT('=') &&
				StartTag[Index] != TEXT('>') &&
				StartTag[Index] != TEXT('/'))
			{
				++Index;
			}
			if (Index <= NameStart)
			{
				break;
			}
			const FString Name = StartTag.Mid(NameStart, Index - NameStart);
			while (Index < StartTag.Len() && FChar::IsWhitespace(StartTag[Index]))
			{
				++Index;
			}
			FString Value;
			if (Index < StartTag.Len() && StartTag[Index] == TEXT('='))
			{
				++Index;
				while (Index < StartTag.Len() && FChar::IsWhitespace(StartTag[Index]))
				{
					++Index;
				}
				if (Index < StartTag.Len() && (StartTag[Index] == TEXT('\'') || StartTag[Index] == TEXT('"')))
				{
					const TCHAR Quote = StartTag[Index++];
					const int32 ValueStart = Index;
					while (Index < StartTag.Len() && StartTag[Index] != Quote)
					{
						++Index;
					}
					Value = StartTag.Mid(ValueStart, Index - ValueStart);
					Index += Index < StartTag.Len() ? 1 : 0;
				}
				else
				{
					const int32 ValueStart = Index;
					while (Index < StartTag.Len() &&
						!FChar::IsWhitespace(StartTag[Index]) &&
						StartTag[Index] != TEXT('>'))
					{
						++Index;
					}
					Value = StartTag.Mid(ValueStart, Index - ValueStart);
				}
			}
			if (Name.Equals(RequestedName, ESearchCase::IgnoreCase))
			{
				return Value;
			}
		}
		return FString();
	}

	void EnsureStylesheetLinkTypes(FString& Document)
	{
		int32 SearchFrom = 0;
		while (SearchFrom < Document.Len())
		{
			int32 LinkStart = Document.Find(TEXT("<link"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			while (LinkStart != INDEX_NONE)
			{
				const int32 BoundaryIndex = LinkStart + 5;
				if (BoundaryIndex >= Document.Len() || IsHtmlNameBoundary(Document[BoundaryIndex]))
				{
					break;
				}
				LinkStart = Document.Find(TEXT("<link"), ESearchCase::IgnoreCase, ESearchDir::FromStart, BoundaryIndex);
			}
			if (LinkStart == INDEX_NONE)
			{
				break;
			}

			const int32 LinkEnd = FindHtmlTagEnd(Document, LinkStart);
			if (LinkEnd == INDEX_NONE)
			{
				break;
			}

			const FString StartTag = Document.Mid(LinkStart, LinkEnd - LinkStart + 1);
			const FString Rel = ReadHtmlAttribute(StartTag, TEXT("rel"));
			const FString Href = ReadHtmlAttribute(StartTag, TEXT("href"));
			const FString Type = ReadHtmlAttribute(StartTag, TEXT("type"));
			if (Rel.Equals(TEXT("stylesheet"), ESearchCase::IgnoreCase) && !Href.IsEmpty() && Type.IsEmpty())
			{
				int32 InsertAt = LinkEnd;
				int32 BeforeEnd = LinkEnd - 1;
				while (BeforeEnd > LinkStart && FChar::IsWhitespace(Document[BeforeEnd]))
				{
					--BeforeEnd;
				}
				if (BeforeEnd > LinkStart && Document[BeforeEnd] == TEXT('/'))
				{
					InsertAt = BeforeEnd;
				}
				Document.InsertAt(InsertAt, TEXT(" type=\"text/css\""));
				SearchFrom = LinkEnd + 17;
			}
			else
			{
				SearchFrom = LinkEnd + 1;
			}
		}
	}

	void ExtractPageScripts(
		FString& Document,
		const FString& DocumentURL,
		TArray<FH5UI_ScriptSource>& OutScripts,
		TArray<FString>& OutErrors)
	{
		int32 SearchFrom = 0;
		while (SearchFrom < Document.Len())
		{
			int32 ScriptStart = Document.Find(
				TEXT("<script"),
				ESearchCase::IgnoreCase,
				ESearchDir::FromStart,
				SearchFrom);
			while (ScriptStart != INDEX_NONE)
			{
				const int32 BoundaryIndex = ScriptStart + 7;
				if (BoundaryIndex >= Document.Len() || IsHtmlNameBoundary(Document[BoundaryIndex]))
				{
					break;
				}
				ScriptStart = Document.Find(
					TEXT("<script"),
					ESearchCase::IgnoreCase,
					ESearchDir::FromStart,
					BoundaryIndex);
			}
			if (ScriptStart == INDEX_NONE)
			{
				break;
			}

			const int32 StartTagEnd = FindHtmlTagEnd(Document, ScriptStart);
			if (StartTagEnd == INDEX_NONE)
			{
				OutErrors.Add(FString::Printf(TEXT("Unterminated <script> tag in %s."), *DocumentURL));
				break;
			}

			const FString StartTag = Document.Mid(ScriptStart, StartTagEnd - ScriptStart + 1);
			const bool bSelfClosing = StartTag.TrimEnd().EndsWith(TEXT("/>"));
			const int32 ContentStart = StartTagEnd + 1;
			const int32 CloseTagStart = bSelfClosing
				? ContentStart
				: Document.Find(TEXT("</script"), ESearchCase::IgnoreCase, ESearchDir::FromStart, ContentStart);
			if (CloseTagStart == INDEX_NONE)
			{
				OutErrors.Add(FString::Printf(TEXT("Missing </script> in %s."), *DocumentURL));
				break;
			}
			const int32 CloseTagEnd = bSelfClosing ? StartTagEnd : FindHtmlTagEnd(Document, CloseTagStart);
			if (CloseTagEnd == INDEX_NONE)
			{
				OutErrors.Add(FString::Printf(TEXT("Unterminated </script> tag in %s."), *DocumentURL));
				break;
			}

			FString Type = ReadHtmlAttribute(StartTag, TEXT("type"));
			Type.TrimStartAndEndInline();
			Type.ToLowerInline();
			const bool bExecutable = Type.IsEmpty() ||
				Type == TEXT("text/javascript") ||
				Type == TEXT("application/javascript") ||
				Type == TEXT("text/ecmascript") ||
				Type == TEXT("application/ecmascript") ||
				Type == TEXT("module");
			if (bExecutable)
			{
				FH5UI_ScriptSource Script;
				const FString SourceAttribute = ReadHtmlAttribute(StartTag, TEXT("src"));
				if (!SourceAttribute.IsEmpty())
				{
					FH5UI_SystemInterface PathJoiner;
					Rml::String JoinedPath;
					PathJoiner.JoinPath(JoinedPath, ToRmlString(DocumentURL), ToRmlString(SourceAttribute));
					Script.SourceURL = FromRmlString(JoinedPath);
					Rml::String ScriptText;
					if (!FH5UI_Module::Get().GetFileInterface().LoadFile(JoinedPath, ScriptText))
					{
						OutErrors.Add(FString::Printf(TEXT("Unable to load JavaScript file: %s"), *Script.SourceURL));
					}
					else
					{
						Script.Code = FromRmlString(ScriptText);
						OutScripts.Add(MoveTemp(Script));
					}
				}
				else
				{
					Script.Code = bSelfClosing ? FString() : Document.Mid(ContentStart, CloseTagStart - ContentStart);
					Script.SourceURL = DocumentURL;
					Script.SourceLine = 1;
					for (int32 Index = 0; Index < ContentStart; ++Index)
					{
						Script.SourceLine += Document[Index] == TEXT('\n') ? 1 : 0;
					}
					OutScripts.Add(MoveTemp(Script));
				}
			}

			const int32 BlockLength = CloseTagEnd - ScriptStart + 1;
			for (int32 Index = ScriptStart; Index <= CloseTagEnd; ++Index)
			{
				if (Document[Index] != TEXT('\r') && Document[Index] != TEXT('\n'))
				{
					Document[Index] = TEXT(' ');
				}
			}
			SearchFrom = ScriptStart + BlockLength;
		}
	}
}

FH5UI_RuntimeView::FH5UI_BridgeEventListener::FH5UI_BridgeEventListener(FH5UI_RuntimeView& InOwner)
	: Owner(InOwner)
{
}

void FH5UI_RuntimeView::FH5UI_BridgeEventListener::ProcessEvent(Rml::Event& Event)
{
	Owner.HandleBridgeEvent(Event);
}

FH5UI_RuntimeView::FH5UI_RuntimeView(
	FReadyCallback InReady,
	FFailedCallback InFailed,
	FEventCallback InEvent,
	FJavaScriptErrorCallback InJavaScriptError)
	: ReadyCallback(MoveTemp(InReady))
	, FailedCallback(MoveTemp(InFailed))
	, EventCallback(MoveTemp(InEvent))
	, JavaScriptErrorCallback(MoveTemp(InJavaScriptError))
{
	const int32 Serial = FPlatformAtomics::InterlockedIncrement(&GH5UI_ContextSerial);
	ContextName = FString::Printf(TEXT("H5UI_View_%d"), Serial);
	Context = Rml::CreateContext(ToRmlString(ContextName), Rml::Vector2i(1, 1));
	BridgeEventListener = MakeUnique<FH5UI_BridgeEventListener>(*this);
	ModelValues.Add(TEXT("viewport.width"), TEXT("1"));
	ModelValues.Add(TEXT("viewport.height"), TEXT("1"));
	ModelValues.Add(TEXT("viewport.renderWidth"), TEXT("1"));
	ModelValues.Add(TEXT("viewport.renderHeight"), TEXT("1"));
	ModelValues.Add(TEXT("viewport.devicePixelRatio"), TEXT("1"));
	ModelValues.Add(TEXT("viewport.renderScale"), TEXT("1"));
	ModelValues.Add(TEXT("viewport.effectivePixelRatio"), TEXT("1"));

	if (!Context)
	{
		Fail(TEXT("Unable to create the native HTML context."));
	}
}

FH5UI_RuntimeView::~FH5UI_RuntimeView()
{
	Close();
	if (Context)
	{
		Rml::RemoveContext(ToRmlString(ContextName));
		Context = nullptr;
	}
	BridgeEventListener.Reset();
}

bool FH5UI_RuntimeView::LoadURL(const FString& URL)
{
	if (!Context)
	{
		return false;
	}

	State = EH5UI_ViewState::Loading;
	CurrentURL = URL;
	bStrategyControlGeometryLogged = false;
	Rml::String DocumentText;
	if (!FH5UI_Module::Get().GetFileInterface().LoadFile(ToRmlString(URL), DocumentText))
	{
		Fail(FString::Printf(TEXT("Unable to load UI document: %s"), *URL));
		return false;
	}

	return LoadDocumentText(FromRmlString(DocumentText), URL);
}

bool FH5UI_RuntimeView::LoadString(const FString& Html, const FString& BaseURL)
{
	State = EH5UI_ViewState::Loading;
	CurrentURL = BaseURL.IsEmpty() ? TEXT("[memory]") : BaseURL;
	bStrategyControlGeometryLogged = false;
	return LoadDocumentText(Html, CurrentURL);
}

bool FH5UI_RuntimeView::Reload()
{
	if (CurrentURL.IsEmpty() || CurrentURL == TEXT("[memory]"))
	{
		return false;
	}

	const FString URLToReload = CurrentURL;
	return LoadURL(URLToReload);
}

void FH5UI_RuntimeView::Close()
{
	ScriptRuntime.Reset();
	if (Document)
	{
		Document->Close();
		Document = nullptr;
	}
	State = EH5UI_ViewState::Unloaded;
	GeneratedPseudoSelectors.Reset();
	PerformanceStats = FH5UI_PerformanceStats();
	NextPerformancePublishTime = 0.0;
#if WITH_DEV_AUTOMATION_TESTS
	BindingContentWriteCount = 0;
	PeriodicBindingElementSyncCount = 0;
	PerformanceModelPublishCount = 0;
#endif
}

bool FH5UI_RuntimeView::FocusElementById(const FString& ElementId)
{
	if (!Document || ElementId.IsEmpty())
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	if (!Element)
	{
		return false;
	}

	MarkActive();
	const bool bFocused = Element->Focus(true);
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bFocused;
}

bool FH5UI_RuntimeView::ExecuteJavaScript(
	const FString& Script,
	FString& OutResult,
	FString& OutError)
{
	if (!bJavaScriptEnabled)
	{
		OutResult.Reset();
		OutError = TEXT("JavaScript is disabled for this H5 UI View.");
		return false;
	}
	if (!ScriptRuntime || !ScriptRuntime->IsValid())
	{
		OutResult.Reset();
		OutError = TEXT("Load a document before executing JavaScript.");
		return false;
	}
	return ScriptRuntime->Execute(Script, CurrentURL + TEXT("#blueprint"), OutResult, OutError);
}

bool FH5UI_RuntimeView::DispatchHtmlEvent(const FString& EventName, const FString& Detail)
{
	if (!Document || EventName.IsEmpty())
	{
		return false;
	}

	Rml::Dictionary Parameters;
	Parameters["detail"] = ToRmlString(Detail);
	MarkActive();
	const bool bDispatched = Document->DispatchEvent(ToRmlString(EventName), Parameters, false, false);
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bDispatched;
}

bool FH5UI_RuntimeView::HasEditableFocus() const
{
	Rml::Element* Element = Context ? Context->GetFocusElement() : nullptr;
	while (Element)
	{
		FString TagName = FromRmlString(Element->GetTagName());
		TagName.ToLowerInline();
		if (TagName == TEXT("textarea") || TagName == TEXT("select"))
		{
			return true;
		}
		if (TagName == TEXT("input"))
		{
			FString InputType = FromRmlString(Element->GetAttribute<Rml::String>("type", "text"));
			InputType.ToLowerInline();
			return IsEditableInputType(InputType);
		}

		FString ContentEditable = FromRmlString(Element->GetAttribute<Rml::String>("contenteditable", "false"));
		if (ContentEditable.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
			ContentEditable.Equals(TEXT("plaintext-only"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		Element = Element->GetParentNode();
	}
	return false;
}

void FH5UI_RuntimeView::BlurFocusedElement()
{
	if (Context)
	{
		if (Rml::Element* FocusedElement = Context->GetFocusElement())
		{
			FocusedElement->Blur();
			if (ScriptRuntime)
			{
				ScriptRuntime->FlushPendingJobs();
			}
		}
	}
}

void FH5UI_RuntimeView::SetTargetFrameRate(int32 InFrameRate)
{
	TargetFrameRate = FMath::Clamp(InFrameRate, 1, 240);
	MarkActive();
}

void FH5UI_RuntimeView::SetJavaScriptEnabled(bool bEnabled)
{
	bJavaScriptEnabled = bEnabled;
	if (!bJavaScriptEnabled)
	{
		ScriptRuntime.Reset();
	}
}

void FH5UI_RuntimeView::SetData(const FString& Name, const FString& Value)
{
	ModelValues.FindOrAdd(Name) = Value;
	MarkActive();
}

void FH5UI_RuntimeView::SynchronizeModels()
{
	CSV_SCOPED_TIMING_STAT(H5UIBindings, SynchronizeModels);
	if (!Document || bSynchronizingModels)
	{
		return;
	}

	TGuardValue<bool> SynchronizingGuard(bSynchronizingModels, true);
	SynchronizeElement(Document);
	MarkActive();
}

bool FH5UI_RuntimeView::Update(
	const FVector2D& LocalSize,
	double CurrentTime,
	float InScreenPixelScale,
	float InRenderScale)
{
	if (!Context)
	{
		return false;
	}

	const float SafeScreenPixelScale = FMath::RoundToFloat(
		FMath::Clamp(InScreenPixelScale, 0.25f, 8.0f) * 64.0f) / 64.0f;
	const float SafeRenderScale = FMath::RoundToFloat(
		FMath::Clamp(InRenderScale, 0.5f, 4.0f) * 64.0f) / 64.0f;
	// A parent ScaleBox can expose a large design-space geometry and then shrink
	// it below 1.0. Use the final visible size as the browser CSS viewport, while
	// the renderer applies the inverse scale so CSS pixels remain visually stable.
	const float SafeLayoutViewportScale = FMath::Min(SafeScreenPixelScale, 1.0f);
	// RenderScale is an explicit authoring-quality multiplier. A UMG parent can
	// legitimately scale a view below 1.0, but that must not cancel an explicit
	// RenderScale request and collapse the glyph atlas back to 1x.
	const float SafeRasterizationScale = FMath::RoundToFloat(
		FMath::Clamp(FMath::Max(SafeScreenPixelScale, 1.0f) * SafeRenderScale, 1.0f, 4.0f) * 64.0f) / 64.0f;
	const bool bScreenPixelScaleChanged = !FMath::IsNearlyEqual(ScreenPixelScale, SafeScreenPixelScale);
	const bool bRenderScaleChanged = !FMath::IsNearlyEqual(RenderScale, SafeRenderScale);
	const bool bRasterizationScaleChanged = !FMath::IsNearlyEqual(FontRasterizationScale, SafeRasterizationScale);
	ScreenPixelScale = SafeScreenPixelScale;
	LayoutViewportScale = SafeLayoutViewportScale;
	RenderScale = SafeRenderScale;
	Rml::GetFontEngineInterface()->SetRasterizationScale(SafeRasterizationScale);
	if (bScreenPixelScaleChanged)
	{
		// The parent Slate transform is already represented by LayoutViewportScale
		// and the inverse renderer scale. Applying a sub-1.0 density here as well
		// would shrink CSS px lengths a second time and change browser layout.
		Context->SetDensityIndependentPixelRatio(FMath::Max(ScreenPixelScale, 1.0f));
		MarkActive();
	}
	if (bRasterizationScaleChanged)
	{
		FontRasterizationScale = SafeRasterizationScale;
		// A sub-1.0 parent transform keeps the Rml density ratio at 1.0, so
		// SetDensityIndependentPixelRatio may not invalidate existing handles.
		// Always resolve new font faces when the physical raster scale changes.
		Context->DirtyFontFaces();
		MarkActive();
	}

	const FIntPoint NewSize(
		FMath::Max(1, FMath::RoundToInt(LocalSize.X * LayoutViewportScale)),
		FMath::Max(1, FMath::RoundToInt(LocalSize.Y * LayoutViewportScale)));
	const bool bViewSizeChanged = NewSize != ViewSize;
	if (bViewSizeChanged)
	{
		ViewSize = NewSize;
		Context->SetDimensions(Rml::Vector2i(ViewSize.X, ViewSize.Y));
		MarkActive();
	}
	const FIntPoint NewRenderSize(
		FMath::Max(1, FMath::RoundToInt(static_cast<float>(ViewSize.X) * FontRasterizationScale)),
		FMath::Max(1, FMath::RoundToInt(static_cast<float>(ViewSize.Y) * FontRasterizationScale)));
	const bool bRenderSizeChanged = NewRenderSize != RenderSize;
	if (bRenderSizeChanged)
	{
		RenderSize = NewRenderSize;
		MarkActive();
	}
	const bool bViewportMetricsChanged = bViewSizeChanged || bRenderSizeChanged || bScreenPixelScaleChanged || bRenderScaleChanged;
	if (bViewportMetricsChanged)
	{
		ModelValues.FindOrAdd(TEXT("viewport.width")) = FString::FromInt(ViewSize.X);
		ModelValues.FindOrAdd(TEXT("viewport.height")) = FString::FromInt(ViewSize.Y);
		ModelValues.FindOrAdd(TEXT("viewport.renderWidth")) = FString::FromInt(RenderSize.X);
		ModelValues.FindOrAdd(TEXT("viewport.renderHeight")) = FString::FromInt(RenderSize.Y);
		ModelValues.FindOrAdd(TEXT("viewport.devicePixelRatio")) = FString::SanitizeFloat(ScreenPixelScale);
		ModelValues.FindOrAdd(TEXT("viewport.renderScale")) = FString::SanitizeFloat(RenderScale);
		ModelValues.FindOrAdd(TEXT("viewport.effectivePixelRatio")) = FString::SanitizeFloat(FontRasterizationScale);
		if (ScriptRuntime)
		{
			ScriptRuntime->SetViewportMetrics(
				ViewSize,
				RenderSize,
				ScreenPixelScale,
				RenderScale,
				FontRasterizationScale);
		}
		MarkActive();
	}
	PerformanceStats.RenderSize = RenderSize;
	PerformanceStats.ScreenPixelScale = ScreenPixelScale;
	PerformanceStats.RenderScale = RenderScale;
	PerformanceStats.EffectivePixelRatio = FontRasterizationScale;

	const double ScriptWakeTime = ScriptRuntime
		? ScriptRuntime->GetNextWakeTimeSeconds()
		: TNumericLimits<double>::Max();
	if (CurrentTime < FMath::Min(NextUpdateTime, ScriptWakeTime))
	{
		return false;
	}

	// Consume the request before running script/layout. A nested MarkActive()
	// during this update then remains queued for the following frame.
	const bool bPaintRequestedForThisUpdate = bPaintRequested;
	bPaintRequested = false;
	PublishPerformanceStats(CurrentTime);
	const double StartTime = FPlatformTime::Seconds();
	const bool bScriptUpdated = ScriptRuntime && ScriptRuntime->Tick(CurrentTime);
	const bool bGeneratedPseudoElements = ApplyGeneratedPseudoElements();
	const bool bUpdated = Context->Update() || bScriptUpdated || bGeneratedPseudoElements;
	bLastUpdateRequestedPaintOnly = bPaintRequestedForThisUpdate && !bUpdated;
	if (bLastUpdateRequestedPaintOnly)
	{
		++PaintOnlyUpdateCount;
		if (PaintOnlyUpdateCount <= 3 || PaintOnlyUpdateCount % 30 == 0)
		{
			UE_LOG(
				LogH5UIPointerRepaintTrace,
				Warning,
				TEXT("[SISH5UI PointerTrace][Repaint] requested "
					"Count=%llu URL=%s ActiveAge=%.3f"),
				PaintOnlyUpdateCount,
				*CurrentURL,
				CurrentTime - LastActivityTime);
		}
	}
	if (!bStrategyControlGeometryLogged && CurrentURL.EndsWith(TEXT("/StrategyControl/strategy-control.html")) && Document)
	{
		Rml::Element* MapFrame = Document->GetElementById("map-frame");
		Rml::Element* SidePanel = Document->GetElementById("side-panel");
		if (MapFrame && SidePanel)
		{
			const Rml::Vector2f MapPosition = MapFrame->GetAbsoluteOffset(Rml::BoxArea::Border);
			const Rml::Vector2f MapSize = MapFrame->GetBox().GetSize(Rml::BoxArea::Border);
			const Rml::Vector2f SidePosition = SidePanel->GetAbsoluteOffset(Rml::BoxArea::Border);
			const Rml::Vector2f SideSize = SidePanel->GetBox().GetSize(Rml::BoxArea::Border);
			UE_LOG(LogH5UIStrategyControlLayout, Verbose,
				TEXT("StrategyControl PIE geometry: viewport=%dx%d screenScale=%.3f renderScale=%.3f map=(%.1f,%.1f %.1fx%.1f) side=(%.1f,%.1f %.1fx%.1f)"),
				ViewSize.X, ViewSize.Y, ScreenPixelScale, RenderScale,
				MapPosition.x, MapPosition.y, MapSize.x, MapSize.y,
				SidePosition.x, SidePosition.y, SideSize.x, SideSize.y);
			if (SidePosition.x < 0.0f || SidePosition.x + SideSize.x > static_cast<float>(ViewSize.X) + 1.0f)
			{
				UE_LOG(LogH5UIStrategyControlLayout, Warning,
					TEXT("StrategyControl side panel is outside the CSS viewport: viewport=%dx%d side=(%.1f,%.1f %.1fx%.1f) screenScale=%.3f"),
					ViewSize.X, ViewSize.Y, SidePosition.x, SidePosition.y, SideSize.x, SideSize.y, ScreenPixelScale);
			}
			bStrategyControlGeometryLogged = true;
		}
	}
	LastUpdateMilliseconds = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);

	const UH5UI_Settings* Settings = GetDefault<UH5UI_Settings>();
	const double ActiveDelay = 1.0 / static_cast<double>(TargetFrameRate);
	const double IdleDelay = 1.0 / static_cast<double>(FMath::Max(1, Settings->IdleUpdateRate));
	const bool bWithinActiveWindow = CurrentTime - LastActivityTime < FMath::Max(0.0f, Settings->IdleAfterSeconds);
	double ScheduledDelay = ActiveDelay;
	if (!bWithinActiveWindow)
	{
		ScheduledDelay = FMath::Max(ActiveDelay, FMath::Min(Context->GetNextUpdateDelay(), IdleDelay));
	}
	NextUpdateTime = CurrentTime + ScheduledDelay;
	if (ScriptRuntime)
	{
		NextUpdateTime = FMath::Min(NextUpdateTime, ScriptRuntime->GetNextWakeTimeSeconds());
	}
	return bUpdated || bPaintRequestedForThisUpdate;
}

int32 FH5UI_RuntimeView::Paint(
	const FGeometry& Geometry,
	FSlateWindowElementList& ElementList,
	int32 LayerId)
{
	if (!Context || State != EH5UI_ViewState::Ready)
	{
		return LayerId;
	}

	FH5UI_RenderInterface& Renderer = FH5UI_Module::Get().GetRenderInterface();
	Renderer.BeginPaint(Geometry, ElementList, LayerId, 1.0f / LayoutViewportScale);
	Context->Render();
	if (bLastUpdateRequestedPaintOnly)
	{
		++PaintOnlySubmissionCount;
		if (PaintOnlySubmissionCount <= 3 || PaintOnlySubmissionCount % 30 == 0)
		{
			UE_LOG(
				LogH5UIPointerRepaintTrace,
				Warning,
				TEXT("[SISH5UI PointerTrace][Repaint] submitted "
					"Count=%llu URL=%s"),
				PaintOnlySubmissionCount,
				*CurrentURL);
		}
		bLastUpdateRequestedPaintOnly = false;
	}
	PerformanceStats = Renderer.EndPaint(ViewSize);
	PerformanceStats.RenderSize = RenderSize;
	PerformanceStats.ScreenPixelScale = ScreenPixelScale;
	PerformanceStats.RenderScale = RenderScale;
	PerformanceStats.EffectivePixelRatio = FontRasterizationScale;
	PerformanceStats.UpdateMilliseconds = LastUpdateMilliseconds;
	if (ScriptRuntime)
	{
		PerformanceStats.JavaScriptMilliseconds = ScriptRuntime->GetLastExecutionMilliseconds();
		PerformanceStats.JavaScriptHeapBytes = ScriptRuntime->GetMemoryUsageBytes();
		PerformanceStats.JavaScriptTimers = ScriptRuntime->GetTimerCount();
	}
	return LayerId + FMath::Max(1, PerformanceStats.DrawBatches);
}

bool FH5UI_RuntimeView::GetBrowserSubview(FH5UI_BrowserSubview& OutSubview) const
{
	OutSubview = FH5UI_BrowserSubview();
	if (!Document)
	{
		return false;
	}

	Rml::Element* IFrame = FindDescendantByTag(Document, "iframe");
	if (!IFrame || !IFrame->IsVisible(true))
	{
		return false;
	}

	const Rml::Vector2f Position = IFrame->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f Size = IFrame->GetBox().GetSize(Rml::BoxArea::Border);
	if (Size.x <= 0.0f || Size.y <= 0.0f)
	{
		return false;
	}

	const Rml::String SourceAttribute = IFrame->GetAttribute<Rml::String>("src", "");
	if (SourceAttribute.empty())
	{
		return false;
	}

	Rml::String ResolvedSource = SourceAttribute;
	if (Rml::SystemInterface* SystemInterface = Rml::GetSystemInterface())
	{
		SystemInterface->JoinPath(ResolvedSource, Document->GetSourceURL(), SourceAttribute);
	}

	OutSubview.Source = FromRmlString(ResolvedSource);
	const float CoordinateScale = 1.0f / LayoutViewportScale;
	OutSubview.Position = FVector2D(Position.x, Position.y) * CoordinateScale;
	OutSubview.Size = FVector2D(Size.x, Size.y) * CoordinateScale;
	return !OutSubview.Source.IsEmpty();
}

bool FH5UI_RuntimeView::ProcessMouseMove(const FVector2D& LocalPosition, const FPointerEvent& Event)
{
	return ProcessCapturedMouseMove(LocalPosition, H5UI_Input::GetModifiers(Event));
}

bool FH5UI_RuntimeView::ProcessCapturedMouseMove(
	const FVector2D& LocalPosition,
	int32 Modifiers)
{
	if (!Context)
	{
		return false;
	}
	MarkActive();
	const bool bHandled = !Context->ProcessMouseMove(
		FMath::RoundToInt(LocalPosition.X * LayoutViewportScale),
		FMath::RoundToInt(LocalPosition.Y * LayoutViewportScale),
		Modifiers);
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bHandled;
}

bool FH5UI_RuntimeView::ProcessMouseButtonDown(
	const FVector2D& LocalPosition,
	const FPointerEvent& Event)
{
	const int32 Button = H5UI_Input::GetMouseButtonIndex(Event.GetEffectingButton());
	if (!Context || Button == INDEX_NONE)
	{
		return false;
	}
	MarkActive();
	ProcessMouseMove(LocalPosition, Event);
	const bool bInteracting = !Context->ProcessMouseButtonDown(Button, H5UI_Input::GetModifiers(Event));
	if (Button == 0 && !bInteracting)
	{
		BlurFocusedElement();
	}
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bInteracting;
}

bool FH5UI_RuntimeView::ProcessMouseButtonUp(
	const FVector2D& LocalPosition,
	const FPointerEvent& Event)
{
	const int32 Button = H5UI_Input::GetMouseButtonIndex(Event.GetEffectingButton());
	return ReleaseCapturedMouseButton(
		LocalPosition,
		Button,
		H5UI_Input::GetModifiers(Event));
}

bool FH5UI_RuntimeView::ReleaseCapturedMouseButton(
	const FVector2D& LocalPosition,
	int32 Button,
	int32 Modifiers)
{
	if (!Context || Button == INDEX_NONE)
	{
		return false;
	}
	MarkActive();
	ProcessCapturedMouseMove(LocalPosition, Modifiers);
	const bool bHandled = !Context->ProcessMouseButtonUp(Button, Modifiers);
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bHandled;
}

bool FH5UI_RuntimeView::ProcessMouseWheel(
	const FVector2D& LocalPosition,
	const FPointerEvent& Event)
{
	if (!Context)
	{
		return false;
	}
	MarkActive();
	ProcessMouseMove(LocalPosition, Event);
	const bool bHandled = !Context->ProcessMouseWheel(
		Rml::Vector2f(0.0f, -Event.GetWheelDelta()),
		H5UI_Input::GetModifiers(Event));
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bHandled;
}

bool FH5UI_RuntimeView::IsPointerInteractingAt(const FVector2D& LocalPosition)
{
	if (!Context)
	{
		return false;
	}

	// A cross-view drag only needs a pointer-events-aware hit test. Processing a
	// synthetic mouse move here would also run page handlers in every candidate
	// view once per frame and could mutate page state while merely probing it.
	Rml::Element* HitElement = Context->GetElementAtPoint(Rml::Vector2f(
		static_cast<float>(LocalPosition.X * LayoutViewportScale),
		static_cast<float>(LocalPosition.Y * LayoutViewportScale)));
	return HitElement != nullptr && HitElement != Context->GetRootElement();
}

FVector2D FH5UI_RuntimeView::LocalToDocumentPosition(const FVector2D& LocalPosition) const
{
	return LocalPosition * LayoutViewportScale;
}

void FH5UI_RuntimeView::ProcessMouseLeave()
{
	if (Context)
	{
		MarkActive();
		Context->ProcessMouseLeave();
		if (ScriptRuntime)
		{
			ScriptRuntime->FlushPendingJobs();
		}
	}
}

bool FH5UI_RuntimeView::ProcessKeyDown(const FKeyEvent& Event)
{
	const Rml::Input::KeyIdentifier Key = H5UI_Input::TranslateKey(Event.GetKey());
	if (!Context || Key == Rml::Input::KI_UNKNOWN)
	{
		return false;
	}
	MarkActive();
	const bool bHandled = !Context->ProcessKeyDown(Key, H5UI_Input::GetModifiers(Event));
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bHandled;
}

bool FH5UI_RuntimeView::ProcessKeyUp(const FKeyEvent& Event)
{
	const Rml::Input::KeyIdentifier Key = H5UI_Input::TranslateKey(Event.GetKey());
	if (!Context || Key == Rml::Input::KI_UNKNOWN)
	{
		return false;
	}
	MarkActive();
	const bool bHandled = !Context->ProcessKeyUp(Key, H5UI_Input::GetModifiers(Event));
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bHandled;
}

bool FH5UI_RuntimeView::ProcessKeyChar(const FCharacterEvent& Event)
{
	const TCHAR Character = Event.GetCharacter();
	if (!Context || Character < TEXT(' '))
	{
		return false;
	}
	MarkActive();
	const bool bHandled = !Context->ProcessTextInput(static_cast<Rml::Character>(Character));
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bHandled;
}

EH5UI_ViewState FH5UI_RuntimeView::GetState() const
{
	return State;
}

const FH5UI_PerformanceStats& FH5UI_RuntimeView::GetPerformanceStats() const
{
	return PerformanceStats;
}

#if WITH_DEV_AUTOMATION_TESTS
FVector2D FH5UI_RuntimeView::GetElementBorderSizeForTesting(const FString& ElementId) const
{
	Rml::Element* Element = Document;
	if (Element && !ElementId.IsEmpty())
	{
		Element = Document->GetElementById(ToRmlString(ElementId));
	}
	if (!Element)
	{
		return FVector2D::ZeroVector;
	}

	const Rml::Vector2f Size = Element->GetBox().GetSize(Rml::BoxArea::Border);
	return FVector2D(Size.x, Size.y);
}

FVector2D FH5UI_RuntimeView::GetElementBorderPositionForTesting(const FString& ElementId) const
{
	if (!Document)
	{
		return FVector2D::ZeroVector;
	}

	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	if (!Element)
	{
		return FVector2D::ZeroVector;
	}

	const Rml::Vector2f Position = Element->GetAbsoluteOffset(Rml::BoxArea::Border);
	return FVector2D(Position.x, Position.y);
}

bool FH5UI_RuntimeView::HasElementFontFaceForTesting(const FString& ElementId) const
{
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	return Element && Element->GetFontFaceHandle() != 0;
}

float FH5UI_RuntimeView::GetElementFontRasterizationScaleForTesting(const FString& ElementId) const
{
	if (!Document)
	{
		return 0.0f;
	}

	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	return Element
		? Rml::GetFontEngineInterface()->GetRasterizationScale(Element->GetFontFaceHandle())
		: 0.0f;
}

bool FH5UI_RuntimeView::HasElementTransformForTesting(const FString& ElementId) const
{
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	return Element && Element->GetComputedValues().has_local_transform();
}

float FH5UI_RuntimeView::GetElementTopLeftBorderRadiusForTesting(const FString& ElementId) const
{
	if (!Document)
	{
		return 0.0f;
	}

	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	return Element ? Element->GetRenderBox(Rml::BoxArea::Border).GetBorderRadius()[0] : 0.0f;
}

FVector2D FH5UI_RuntimeView::GetDescendantBorderSizeForTesting(
	const FString& ElementId,
	const FString& TagName) const
{
	Rml::Element* Element = Document;
	if (Element && !ElementId.IsEmpty())
	{
		Element = Document->GetElementById(ToRmlString(ElementId));
	}
	Element = FindDescendantByTag(Element, ToRmlString(TagName));
	if (!Element)
	{
		return FVector2D::ZeroVector;
	}

	const Rml::Vector2f Size = Element->GetBox().GetSize(Rml::BoxArea::Border);
	return FVector2D(Size.x, Size.y);
}

FVector2D FH5UI_RuntimeView::GetDescendantBorderPositionForTesting(
	const FString& ElementId,
	const FString& TagName) const
{
	Rml::Element* Element = Document;
	if (Element && !ElementId.IsEmpty())
	{
		Element = Document->GetElementById(ToRmlString(ElementId));
	}
	Element = FindDescendantByTag(Element, ToRmlString(TagName));
	if (!Element)
	{
		return FVector2D::ZeroVector;
	}

	const Rml::Vector2f Position = Element->GetAbsoluteOffset(Rml::BoxArea::Border);
	return FVector2D(Position.x, Position.y);
}

FVector2D FH5UI_RuntimeView::GetDirectTextVisualCenterForTesting(const FString& ElementId) const
{
	if (!Document)
	{
		return FVector2D::ZeroVector;
	}

	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	if (!Element)
	{
		return FVector2D::ZeroVector;
	}

	for (int32 ChildIndex = 0; ChildIndex < Element->GetNumChildren(); ++ChildIndex)
	{
		Rml::ElementText* TextElement = rmlui_dynamic_cast<Rml::ElementText*>(Element->GetChild(ChildIndex));
		if (!TextElement || TextElement->GetLines().empty())
		{
			continue;
		}

		const Rml::ElementText::Line& Line = TextElement->GetLines().front();
		const Rml::Vector2f Baseline = TextElement->GetAbsoluteOffset(Rml::BoxArea::Border) + Line.position;
		const Rml::FontMetrics& Metrics = Rml::GetFontEngineInterface()->GetFontMetrics(TextElement->GetFontFaceHandle());
		const float TextWidth = static_cast<float>(Rml::ElementUtilities::GetStringWidth(TextElement, Line.text));
		return FVector2D(
			Baseline.x + 0.5f * TextWidth,
			Baseline.y + 0.5f * (Metrics.descent - Metrics.ascent));
	}

	return FVector2D::ZeroVector;
}

int32 FH5UI_RuntimeView::GetDirectTextLineCountForTesting(const FString& ElementId) const
{
	if (!Document)
	{
		return 0;
	}

	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	if (!Element)
	{
		return 0;
	}

	int32 LineCount = 0;
	for (int32 ChildIndex = 0; ChildIndex < Element->GetNumChildren(); ++ChildIndex)
	{
		if (Rml::ElementText* TextElement = rmlui_dynamic_cast<Rml::ElementText*>(Element->GetChild(ChildIndex)))
		{
			LineCount += static_cast<int32>(TextElement->GetLines().size());
		}
	}
	return LineCount;
}

FString FH5UI_RuntimeView::GetDescendantInnerRmlForTesting(
	const FString& ElementId,
	const FString& TagName) const
{
	Rml::Element* Element = Document;
	if (Element && !ElementId.IsEmpty())
	{
		Element = Document->GetElementById(ToRmlString(ElementId));
	}
	Element = FindDescendantByTag(Element, ToRmlString(TagName));
	if (!Element)
	{
		return FString();
	}
	Rml::String InnerRml;
	Element->GetInnerRML(InnerRml);
	return FromRmlString(InnerRml);
}

bool FH5UI_RuntimeView::HasElementAttributeForTesting(
	const FString& ElementId,
	const FString& AttributeName) const
{
	if (!Document)
	{
		return false;
	}
	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	return Element && Element->HasAttribute(ToRmlString(AttributeName));
}

FString FH5UI_RuntimeView::GetElementAttributeForTesting(
	const FString& ElementId,
	const FString& AttributeName) const
{
	if (!Document)
	{
		return FString();
	}
	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	return Element
		? FromRmlString(Element->GetAttribute<Rml::String>(ToRmlString(AttributeName), Rml::String()))
		: FString();
}

FString FH5UI_RuntimeView::GetElementValueForTesting(const FString& ElementId) const
{
	if (!Document)
	{
		return FString();
	}
	Rml::ElementFormControl* FormControl = rmlui_dynamic_cast<Rml::ElementFormControl*>(
		Document->GetElementById(ToRmlString(ElementId)));
	return FormControl ? FromRmlString(FormControl->GetValue()) : FString();
}

FString FH5UI_RuntimeView::GetElementInnerRmlForTesting(const FString& ElementId) const
{
	if (!Document)
	{
		return FString();
	}
	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	if (!Element)
	{
		return FString();
	}
	Rml::String InnerRml;
	Element->GetInnerRML(InnerRml);
	return FromRmlString(InnerRml);
}

bool FH5UI_RuntimeView::IsDescendantInsideElementForTesting(
	const FString& ElementId,
	const FString& TagName) const
{
	if (!Document)
	{
		return false;
	}
	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	Rml::Element* Descendant = FindDescendantByTag(Element, ToRmlString(TagName));
	if (!Element || !Descendant)
	{
		return false;
	}

	const Rml::Vector2f ParentPosition = Element->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f ParentSize = Element->GetBox().GetSize(Rml::BoxArea::Border);
	const Rml::Vector2f ChildPosition = Descendant->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f ChildSize = Descendant->GetBox().GetSize(Rml::BoxArea::Border);
	constexpr float Tolerance = 1.0f;
	return ChildPosition.x >= ParentPosition.x - Tolerance &&
		ChildPosition.y >= ParentPosition.y - Tolerance &&
		ChildPosition.x + ChildSize.x <= ParentPosition.x + ParentSize.x + Tolerance &&
		ChildPosition.y + ChildSize.y <= ParentPosition.y + ParentSize.y + Tolerance;
}

bool FH5UI_RuntimeView::IsPointerInteractingAtForTesting(const FVector2D& LocalPosition)
{
	return IsPointerInteractingAt(LocalPosition);
}

bool FH5UI_RuntimeView::ProcessMouseMoveForTesting(const FVector2D& LocalPosition)
{
	return ProcessCapturedMouseMove(LocalPosition);
}

bool FH5UI_RuntimeView::ProcessMouseButtonDownForTesting(const FVector2D& LocalPosition)
{
	if (!Context)
	{
		return false;
	}
	MarkActive();
	ProcessMouseMoveForTesting(LocalPosition);
	const bool bInteracting = !Context->ProcessMouseButtonDown(0, 0);
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bInteracting;
}

bool FH5UI_RuntimeView::ProcessMouseButtonUpForTesting(const FVector2D& LocalPosition)
{
	return ReleaseCapturedMouseButton(LocalPosition, 0);
}

bool FH5UI_RuntimeView::ProcessMouseWheelForTesting(const FVector2D& LocalPosition, float WheelDelta)
{
	if (!Context)
	{
		return false;
	}
	ProcessMouseMoveForTesting(LocalPosition);
	const bool bInteracting = !Context->ProcessMouseWheel(Rml::Vector2f(0.0f, -WheelDelta), 0);
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return bInteracting;
}

bool FH5UI_RuntimeView::ClickElementForTesting(const FString& ElementId)
{
	if (!Document)
	{
		return false;
	}
	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	if (!Element)
	{
		return false;
	}
	Element->Click();
	if (ScriptRuntime)
	{
		ScriptRuntime->FlushPendingJobs();
	}
	return true;
}

bool FH5UI_RuntimeView::HasGeneratedPseudoElementForTesting(const FString& ElementId, bool bBefore) const
{
	if (!Document)
	{
		return false;
	}
	Rml::Element* Element = Document->GetElementById(ToRmlString(ElementId));
	if (!Element)
	{
		return false;
	}
	const Rml::String GeneratedClass = bBefore ? "h5ui-generated-before" : "h5ui-generated-after";
	for (int32 ChildIndex = 0; ChildIndex < Element->GetNumChildren(); ++ChildIndex)
	{
		Rml::Element* Child = Element->GetChild(ChildIndex);
		if (Child && Child->IsClassSet(GeneratedClass))
		{
			return true;
		}
	}
	return false;
}

#endif

bool FH5UI_RuntimeView::LoadDocumentText(const FString& DocumentText, const FString& SourceURL)
{
	Close();
	// RmlUi caches external stylesheets and templates globally by URL. Each new
	// document receives its own combined stylesheet, so invalidating here is safe
	// for other open views and ensures both LoadURL and Reload observe disk edits.
	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();
	State = EH5UI_ViewState::Loading;

	FString NormalizedDocument = DocumentText;
	GeneratedPseudoSelectors.Reset();
	TArray<FH5UI_ScriptSource> PageScripts;
	TArray<FString> ScriptLoadErrors;
	ExtractPageScripts(NormalizedDocument, SourceURL, PageScripts, ScriptLoadErrors);
	FH5UI_FileInterface& FileInterface = FH5UI_Module::Get().GetFileInterface();
	FileInterface.BeginDocumentStyleCapture();
	NormalizeHtmlDocumentForRml(NormalizedDocument, GeneratedPseudoSelectors);

	Rml::GetFontEngineInterface()->SetRasterizationScale(FontRasterizationScale);
	Document = Context->LoadDocumentFromMemory(ToRmlString(NormalizedDocument), ToRmlString(SourceURL));
	for (const FH5UI_GeneratedPseudoSelector& Rule : FileInterface.ConsumeGeneratedPseudoSelectors())
	{
		GeneratedPseudoSelectors.AddUnique(Rule);
	}
	if (!Document)
	{
		Fail(FString::Printf(TEXT("Unable to parse UI document: %s"), *SourceURL));
		return false;
	}

	AttachBridgeListeners(Document);
	ApplyGeneratedPseudoElements();
	SynchronizeModels();
	Document->Show();
	State = EH5UI_ViewState::Ready;
	MarkActive();

	if (bJavaScriptEnabled)
	{
		const UH5UI_Settings* Settings = GetDefault<UH5UI_Settings>();
		ScriptRuntime = MakeUnique<FH5UI_ScriptRuntime>(
			[this](const FString& EventType, const FString& Name, const FString& Payload, const FString& ElementId)
			{
				EmitJavaScriptEvent(EventType, Name, Payload, ElementId);
			},
			[this](const FString& Name)
			{
				return GetJavaScriptData(Name);
			},
			[this](const FString& Name, const FString& Value)
			{
				SetJavaScriptData(Name, Value);
			},
			[this](const FString& Error)
			{
				ReportJavaScriptError(Error);
			},
			[this]()
			{
				MarkActive();
			});
		if (!ScriptRuntime->Initialize(
				Document,
				SourceURL,
				static_cast<int64>(Settings->JavaScriptMemoryLimitMB) * 1024 * 1024,
				Settings->JavaScriptExecutionTimeLimitMilliseconds,
				Settings->JavaScriptPromiseJobTimeLimitMilliseconds,
				Settings->JavaScriptInitialExecutionTimeLimitMilliseconds,
				Settings->JavaScriptMaxCallbacksPerFrame))
		{
			ScriptRuntime.Reset();
		}
		else
		{
			ScriptRuntime->BindInlineEventHandlers();
		}
	}

	if (ReadyCallback)
	{
		ReadyCallback();
	}
	for (const FString& ScriptLoadError : ScriptLoadErrors)
	{
		ReportJavaScriptError(ScriptLoadError);
	}
	if (ScriptRuntime)
	{
		ScriptRuntime->SetViewportMetrics(
			ViewSize,
			RenderSize,
			ScreenPixelScale,
			RenderScale,
			FontRasterizationScale);
		ScriptRuntime->ExecutePageScripts(PageScripts);
		// Page scripts register their window.resize listeners after the initial
		// metrics assignment. Replay once so first-frame layout never depends on a
		// user manually resizing the editor or game window.
		ScriptRuntime->DispatchViewportResize();
		ApplyGeneratedPseudoElements();
		ScriptRuntime->DispatchDocumentReady();
	}
	return true;
}

bool FH5UI_RuntimeView::ApplyGeneratedPseudoElements()
{
	if (!Document || GeneratedPseudoSelectors.IsEmpty())
	{
		return false;
	}

	bool bAddedAny = false;
	for (const FH5UI_GeneratedPseudoSelector& Rule : GeneratedPseudoSelectors)
	{
		Rml::ElementList Matches;
		Document->QuerySelectorAll(Matches, ToRmlString(Rule.Selector));
		const Rml::String GeneratedClass = Rule.bBefore
			? "h5ui-generated-before"
			: "h5ui-generated-after";
		for (Rml::Element* Match : Matches)
		{
			bool bAlreadyGenerated = false;
			for (int32 ChildIndex = 0; ChildIndex < Match->GetNumChildren(); ++ChildIndex)
			{
				Rml::Element* Child = Match->GetChild(ChildIndex);
				if (Child && Child->IsClassSet(GeneratedClass))
				{
					bAlreadyGenerated = true;
					break;
				}
			}
			if (bAlreadyGenerated)
			{
				continue;
			}

			Rml::ElementPtr Generated = Document->CreateElement("span");
			Generated->SetClass(GeneratedClass, true);
			if (Rule.bBefore && Match->GetFirstChild())
			{
				Match->InsertBefore(MoveTemp(Generated), Match->GetFirstChild());
			}
			else
			{
				Match->AppendChild(MoveTemp(Generated));
			}
			bAddedAny = true;
		}
	}
	return bAddedAny;
}

void FH5UI_RuntimeView::AttachBridgeListeners(Rml::Element* Element)
{
	if (!Element)
	{
		return;
	}

	const Rml::String EventName = Element->GetAttribute<Rml::String>("data-ue-event", "");
	const Rml::String ModelName = Element->GetAttribute<Rml::String>("data-ue-model", "");
	Rml::String EventType;
	if (!EventName.empty())
	{
		EventType = Element->GetAttribute<Rml::String>("data-ue-event-type", "click");
		Element->AddEventListener(EventType, BridgeEventListener.Get());
	}
	if (!ModelName.empty() && (EventName.empty() || EventType != "change"))
	{
		Element->AddEventListener("change", BridgeEventListener.Get());
	}

	for (int32 ChildIndex = 0; ChildIndex < Element->GetNumChildren(); ++ChildIndex)
	{
		AttachBridgeListeners(Element->GetChild(ChildIndex));
	}
}

void FH5UI_RuntimeView::SynchronizeElement(Rml::Element* Element)
{
	if (!Element)
	{
		return;
	}
	SynchronizeElementBindings(Element);
	for (int32 ChildIndex = 0; ChildIndex < Element->GetNumChildren(); ++ChildIndex)
	{
		SynchronizeElement(Element->GetChild(ChildIndex));
	}
}

void FH5UI_RuntimeView::SynchronizeElementBindings(Rml::Element* Element)
{
	const FString BindingName = FromRmlString(Element->GetAttribute<Rml::String>("data-bind", ""));
	if (const FString* Value = ModelValues.Find(BindingName))
	{
		Rml::String Content = Rml::StringUtilities::EncodeRml(ToRmlString(*Value));
		// Rml's text factory omits whitespace-only nodes. Compare against that
		// actual representation as well, otherwise a blank binding rewrites forever.
		bool bOnlyWhitespace = true;
		for (const char Character : Content)
		{
			if (!Rml::StringUtilities::IsWhitespace(Character)) { bOnlyWhitespace = false; break; }
		}
		if (bOnlyWhitespace) { Content.clear(); }
		// Compare the live DOM, not a cached model value: script can replace this
		// node or edit its contents without changing the native model.
		if (Element->GetInnerRML() != Content)
		{
			Element->SetInnerRML(Content);
			CSV_CUSTOM_STAT(H5UIBindings, ContentWrites, 1, ECsvCustomStatOp::Accumulate);
#if WITH_DEV_AUTOMATION_TESTS
			++BindingContentWriteCount;
#endif
		}
	}

	const FString ValueBindingName = FromRmlString(Element->GetAttribute<Rml::String>("data-bind-value", ""));
	if (const FString* Value = ModelValues.Find(ValueBindingName))
	{
		const Rml::String AttributeValue = ToRmlString(*Value);
		if (!Element->HasAttribute("value") || Element->GetAttribute<Rml::String>("value", "") != AttributeValue)
		{
			Element->SetAttribute("value", AttributeValue);
		}
	}

	const FString ModelName = FromRmlString(Element->GetAttribute<Rml::String>("data-ue-model", ""));
	if (const FString* Value = ModelValues.Find(ModelName))
	{
		if (Rml::ElementFormControl* FormControl = rmlui_dynamic_cast<Rml::ElementFormControl*>(Element))
		{
			if (Rml::ElementFormControlInput* Input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(Element))
			{
				const Rml::String InputType = Input->GetAttribute<Rml::String>("type", "text");
				if (InputType == "checkbox" || InputType == "radio")
				{
					const bool bChecked = InputType == "checkbox"
						? IsTruthyModelValue(*Value)
						: FormControl->GetValue() == ToRmlString(*Value);
					if (bChecked != Input->HasAttribute("checked"))
					{
						if (bChecked)
						{
							Input->SetAttribute("checked", "");
						}
						else
						{
							Input->RemoveAttribute("checked");
						}
					}
				}
				else if (FormControl->GetValue() != ToRmlString(*Value))
				{
					FormControl->SetValue(ToRmlString(*Value));
				}
			}
			else if (FormControl->GetValue() != ToRmlString(*Value))
			{
				FormControl->SetValue(ToRmlString(*Value));
			}
		}
	}
}

void FH5UI_RuntimeView::SynchronizePeriodicBindings(Rml::Element* Element, bool& bPerformanceValuesPublished)
{
	// Discover bindings in the live tree on every publish. A permanent node cache
	// would miss bindings inserted or renamed by JavaScript after page load.
	bool bHasBinding = false;
	static const Rml::String AttributeNames[] = {"data-bind", "data-bind-value", "data-ue-model"};
	for (const Rml::String& Name : AttributeNames)
	{
		if (Element->HasAttribute(Name))
		{
			bHasBinding = true;
			if (!bPerformanceValuesPublished && Element->GetAttribute<Rml::String>(Name, "").compare(0, 5, "perf.") == 0)
			{
				UpdatePerformanceModelValues();
				bPerformanceValuesPublished = true;
			}
		}
	}
	if (bHasBinding)
	{
		SynchronizeElementBindings(Element);
		CSV_CUSTOM_STAT(H5UIBindings, PeriodicBindingElements, 1, ECsvCustomStatOp::Accumulate);
#if WITH_DEV_AUTOMATION_TESTS
		++PeriodicBindingElementSyncCount;
#endif
	}
	// Synchronization can replace children; inspect them only after the write.
	for (int32 ChildIndex = 0; ChildIndex < Element->GetNumChildren(); ++ChildIndex)
	{
		SynchronizePeriodicBindings(Element->GetChild(ChildIndex), bPerformanceValuesPublished);
	}
}

void FH5UI_RuntimeView::UpdatePerformanceModelValues()
{
#if WITH_DEV_AUTOMATION_TESTS
	++PerformanceModelPublishCount;
#endif
	ModelValues.FindOrAdd(TEXT("perf.update")) = FString::Printf(TEXT("%.3f"), PerformanceStats.UpdateMilliseconds);
	ModelValues.FindOrAdd(TEXT("perf.submit")) = FString::Printf(TEXT("%.3f"), PerformanceStats.SubmitMilliseconds);
	ModelValues.FindOrAdd(TEXT("perf.javascript")) = FString::Printf(TEXT("%.3f"), PerformanceStats.JavaScriptMilliseconds);
	ModelValues.FindOrAdd(TEXT("perf.jsHeap")) = FString::Printf(TEXT("%lld"), PerformanceStats.JavaScriptHeapBytes);
	ModelValues.FindOrAdd(TEXT("perf.jsTimers")) = FString::FromInt(PerformanceStats.JavaScriptTimers);
	ModelValues.FindOrAdd(TEXT("perf.batches")) = FString::FromInt(PerformanceStats.DrawBatches);
	ModelValues.FindOrAdd(TEXT("perf.vertices")) = FString::FromInt(PerformanceStats.Vertices);
	ModelValues.FindOrAdd(TEXT("perf.viewport")) = FString::Printf(TEXT("%d x %d"), ViewSize.X, ViewSize.Y);
	ModelValues.FindOrAdd(TEXT("perf.renderSize")) = FString::Printf(TEXT("%d x %d"), RenderSize.X, RenderSize.Y);
	ModelValues.FindOrAdd(TEXT("perf.screenPixelScale")) = FString::SanitizeFloat(ScreenPixelScale);
	ModelValues.FindOrAdd(TEXT("perf.renderScale")) = FString::SanitizeFloat(RenderScale);
	ModelValues.FindOrAdd(TEXT("perf.effectivePixelRatio")) = FString::SanitizeFloat(FontRasterizationScale);
}

void FH5UI_RuntimeView::PublishPerformanceStats(double CurrentTime)
{
	if (!Document || State != EH5UI_ViewState::Ready || CurrentTime < NextPerformancePublishTime)
	{
		return;
	}
	CSV_SCOPED_TIMING_STAT(H5UIBindings, PeriodicBindings);
	NextPerformancePublishTime = CurrentTime + 0.25;
	// JavaScript may read perf.* through ue.getData without a DOM binding.
	bool bPerformanceValuesPublished = ScriptRuntime.IsValid();
	if (bPerformanceValuesPublished)
	{
		UpdatePerformanceModelValues();
	}
	if (!bSynchronizingModels)
	{
		TGuardValue<bool> SynchronizingGuard(bSynchronizingModels, true);
		SynchronizePeriodicBindings(Document, bPerformanceValuesPublished);
	}
}

void FH5UI_RuntimeView::HandleBridgeEvent(Rml::Event& Event)
{
	if (bSynchronizingModels)
	{
		return;
	}

	Rml::Element* Element = Event.GetCurrentElement();
	if (!Element)
	{
		return;
	}

	const FString ModelName = FromRmlString(Element->GetAttribute<Rml::String>("data-ue-model", ""));
	FString Payload = FromRmlString(Element->GetAttribute<Rml::String>("data-ue-payload", ""));
	bool bShouldUpdateModel = !ModelName.IsEmpty();
	if (Rml::ElementFormControl* FormControl = rmlui_dynamic_cast<Rml::ElementFormControl*>(Element))
	{
		if (Rml::ElementFormControlInput* Input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(Element))
		{
			const Rml::String InputType = Input->GetAttribute<Rml::String>("type", "text");
			if (InputType == "checkbox")
			{
				Payload = Input->HasAttribute("checked") ? TEXT("true") : TEXT("false");
			}
			else if (InputType == "radio")
			{
				const bool bChecked = Input->HasAttribute("checked");
				if (!bChecked)
				{
					return;
				}
				Payload = FromRmlString(FormControl->GetValue());
			}
			else
			{
				Payload = FromRmlString(FormControl->GetValue());
			}
		}
		else
		{
			Payload = FromRmlString(FormControl->GetValue());
		}
	}
	if (bShouldUpdateModel)
	{
		ModelValues.FindOrAdd(ModelName) = Payload;
		SynchronizeModels();
	}

	FString EventName = FromRmlString(Element->GetAttribute<Rml::String>("data-ue-event", ""));
	if (EventName.IsEmpty() && !ModelName.IsEmpty())
	{
		EventName = TEXT("ModelChanged");
	}

	if (!EventName.IsEmpty() && EventCallback)
	{
		FH5UI_Event BridgeEvent;
		BridgeEvent.Name = FName(*EventName);
		BridgeEvent.Payload = Payload;
		BridgeEvent.ElementId = FromRmlString(Element->GetId());
		EventCallback(BridgeEvent);
	}
}

void FH5UI_RuntimeView::EmitJavaScriptEvent(
	const FString& EventType,
	const FString& Name,
	const FString& Payload,
	const FString& ElementId)
{
	if (Name.IsEmpty() || !EventCallback)
	{
		return;
	}
	FH5UI_Event Event;
	Event.Name = FName(*Name);
	Event.EventType = FName(*EventType);
	Event.Payload = Payload;
	Event.ElementId = ElementId;
	EventCallback(Event);
}

FString FH5UI_RuntimeView::GetJavaScriptData(const FString& Name) const
{
	if (const FString* Value = ModelValues.Find(Name))
	{
		return *Value;
	}
	return FString();
}

void FH5UI_RuntimeView::SetJavaScriptData(const FString& Name, const FString& Value)
{
	if (Name.IsEmpty())
	{
		return;
	}
	SetData(Name, Value);
	SynchronizeModels();
}

void FH5UI_RuntimeView::ReportJavaScriptError(const FString& Error) const
{
	if (JavaScriptErrorCallback)
	{
		JavaScriptErrorCallback(Error);
	}
}

void FH5UI_RuntimeView::MarkActive()
{
	LastActivityTime = FPlatformTime::Seconds();
	NextUpdateTime = 0.0;
	bPaintRequested = true;
}

void FH5UI_RuntimeView::Fail(const FString& Error)
{
	State = EH5UI_ViewState::Failed;
	if (FailedCallback)
	{
		FailedCallback(Error);
	}
}
