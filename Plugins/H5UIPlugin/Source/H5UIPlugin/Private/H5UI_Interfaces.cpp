#include "H5UI_Interfaces.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Layout/Clipping.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "H5UI_Settings.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "RmlUi/Core/DecorationTypes.h"
#include "RmlUi/Core/Dictionary.h"
#include "nanosvg.h"
#include "nanosvgrast.h"

DEFINE_LOG_CATEGORY_STATIC(LogH5UIPluginRuntime, Log, All);

namespace H5UIPlugin
{
	FString FromUtf8(const Rml::String& Value)
	{
		return UTF8_TO_TCHAR(Value.c_str());
	}

	Rml::String ToUtf8(const FString& Value)
	{
		return Rml::String(TCHAR_TO_UTF8(*Value));
	}

	FString StripUrlQueryAndFragment(FString Path)
	{
		int32 Index = INDEX_NONE;
		if (Path.FindChar(TEXT('#'), Index))
		{
			Path.LeftInline(Index);
		}
		if (Path.FindChar(TEXT('?'), Index))
		{
			Path.LeftInline(Index);
		}
		return Path;
	}

	/** Collapse newlines/tabs inside balanced func(...) so multi-line CSS gradients parse. */
	void CollapseCssFunctionBodies(FString& Css, const TCHAR* FunctionName)
	{
		const FString OpenToken = FString(FunctionName) + TEXT("(");
		int32 SearchFrom = 0;
		while (SearchFrom < Css.Len())
		{
			const int32 OpenIndex = Css.Find(*OpenToken, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			if (OpenIndex == INDEX_NONE)
			{
				break;
			}

			const int32 ContentStart = OpenIndex + OpenToken.Len();
			int32 Depth = 1;
			int32 Cursor = ContentStart;
			bool bInQuote = false;
			TCHAR QuoteChar = 0;
			while (Cursor < Css.Len() && Depth > 0)
			{
				const TCHAR Ch = Css[Cursor];
				if (bInQuote)
				{
					if (Ch == QuoteChar && (Cursor == 0 || Css[Cursor - 1] != TEXT('\\')))
					{
						bInQuote = false;
					}
				}
				else if (Ch == TEXT('\'') || Ch == TEXT('"'))
				{
					bInQuote = true;
					QuoteChar = Ch;
				}
				else if (Ch == TEXT('('))
				{
					++Depth;
				}
				else if (Ch == TEXT(')'))
				{
					--Depth;
				}
				++Cursor;
			}

			if (Depth != 0)
			{
				SearchFrom = ContentStart;
				continue;
			}

			const int32 ContentEnd = Cursor - 1; // index of ')'
			FString Body = Css.Mid(ContentStart, ContentEnd - ContentStart);
			Body.ReplaceInline(TEXT("\r\n"), TEXT(" "));
			Body.ReplaceInline(TEXT("\n"), TEXT(" "));
			Body.ReplaceInline(TEXT("\r"), TEXT(" "));
			Body.ReplaceInline(TEXT("\t"), TEXT(" "));
			while (Body.ReplaceInline(TEXT("  "), TEXT(" ")))
			{
			}
			Body.TrimStartAndEndInline();
			Css = Css.Left(ContentStart) + Body + Css.Mid(ContentEnd);
			SearchFrom = ContentStart + Body.Len() + 1;
		}
	}

	/** Fold trivial calc(Npx +/- Mpx) so pages authored for browsers still resolve. */
	void FoldTrivialCssCalc(FString& Css)
	{
		int32 SearchFrom = 0;
		while (SearchFrom < Css.Len())
		{
			const int32 CalcIndex = Css.Find(TEXT("calc("), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			if (CalcIndex == INDEX_NONE)
			{
				break;
			}
			const int32 ContentStart = CalcIndex + 5;
			int32 Depth = 1;
			int32 Cursor = ContentStart;
			while (Cursor < Css.Len() && Depth > 0)
			{
				if (Css[Cursor] == TEXT('('))
				{
					++Depth;
				}
				else if (Css[Cursor] == TEXT(')'))
				{
					--Depth;
				}
				++Cursor;
			}
			if (Depth != 0)
			{
				SearchFrom = ContentStart;
				continue;
			}

			const int32 ContentEnd = Cursor - 1;
			FString Expr = Css.Mid(ContentStart, ContentEnd - ContentStart);
			Expr.ReplaceInline(TEXT(" "), TEXT(""));
			// Only fold pure length expressions: 12px+4px / 12px-4px
			float A = 0.f;
			float B = 0.f;
			TCHAR Op = 0;
			bool bMatched = false;
			if (Expr.EndsWith(TEXT("px")))
			{
				// a+/-bpx forms
				for (int32 i = 1; i < Expr.Len(); ++i)
				{
					if (Expr[i] == TEXT('+') || (Expr[i] == TEXT('-') && i > 0))
					{
						const FString Left = Expr.Left(i);
						const FString Right = Expr.Mid(i + 1);
						if (Left.EndsWith(TEXT("px")) && Right.EndsWith(TEXT("px")))
						{
							A = FCString::Atof(*Left.LeftChop(2));
							B = FCString::Atof(*Right.LeftChop(2));
							Op = Expr[i];
							bMatched = true;
						}
						break;
					}
				}
			}
			if (bMatched)
			{
				const float Result = Op == TEXT('+') ? (A + B) : (A - B);
				const FString Replacement = FString::Printf(TEXT("%gpx"), Result);
				Css = Css.Left(CalcIndex) + Replacement + Css.Mid(ContentEnd + 1);
				SearchFrom = CalcIndex + Replacement.Len();
			}
			else
			{
				SearchFrom = ContentEnd + 1;
			}
		}
	}

	void StripCssDeclarationsByPrefix(FString& Css, const TCHAR* PropertyPrefix)
	{
		int32 SearchFrom = 0;
		const FString Prefix(PropertyPrefix);
		while (SearchFrom < Css.Len())
		{
			const int32 PropIndex = Css.Find(*Prefix, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			if (PropIndex == INDEX_NONE)
			{
				break;
			}
			// Only strip at declaration start (after { ; or whitespace run)
			if (PropIndex > 0)
			{
				const TCHAR Prev = Css[PropIndex - 1];
				if (Prev != TEXT('{') && Prev != TEXT(';') && Prev != TEXT('\n') && Prev != TEXT('\r') && Prev != TEXT('\t') && Prev != TEXT(' '))
				{
					SearchFrom = PropIndex + Prefix.Len();
					continue;
				}
			}
			const int32 Semi = Css.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, PropIndex);
			if (Semi == INDEX_NONE)
			{
				break;
			}
			Css.RemoveAt(PropIndex, Semi - PropIndex + 1);
			SearchFrom = PropIndex;
		}
	}

	void StripCssDeclarationWithValue(FString& Css, const TCHAR* PropertyName, const TCHAR* ExpectedValue)
	{
		const FString Prefix = FString(PropertyName) + TEXT(":");
		int32 SearchFrom = 0;
		while (SearchFrom < Css.Len())
		{
			const int32 PropIndex = Css.Find(*Prefix, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			if (PropIndex == INDEX_NONE)
			{
				break;
			}
			if (PropIndex > 0)
			{
				const TCHAR Prev = Css[PropIndex - 1];
				if (Prev != TEXT('{') && Prev != TEXT(';') && !FChar::IsWhitespace(Prev))
				{
					SearchFrom = PropIndex + Prefix.Len();
					continue;
				}
			}

			const int32 ValueStart = PropIndex + Prefix.Len();
			const int32 Semi = Css.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
			const int32 Close = Css.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
			const int32 ValueEnd = Semi != INDEX_NONE && (Close == INDEX_NONE || Semi < Close) ? Semi : Close;
			if (ValueEnd == INDEX_NONE)
			{
				break;
			}

			FString Value = Css.Mid(ValueStart, ValueEnd - ValueStart);
			Value.TrimStartAndEndInline();
			if (Value.Equals(ExpectedValue, ESearchCase::IgnoreCase))
			{
				const int32 RemoveEnd = ValueEnd == Semi ? ValueEnd + 1 : ValueEnd;
				Css.RemoveAt(PropIndex, RemoveEnd - PropIndex);
				SearchFrom = PropIndex;
			}
			else
			{
				SearchFrom = ValueEnd + 1;
			}
		}
	}

	/**
	 * Remove only unsafe comma-separated items from a CSS list declaration.
	 *
	 * Transition lists may contain commas inside functions such as cubic-bezier(),
	 * so split only at the top level. Keeping the remaining items prevents one
	 * unsupported property (for example box-shadow) from disabling otherwise
	 * native width, opacity, and transform transitions.
	 */
	void StripCssListItemsContainingValue(FString& Css, const TCHAR* PropertyName, const TCHAR* ValueFragment)
	{
		const FString Prefix = FString(PropertyName) + TEXT(":");
		int32 SearchFrom = 0;
		while (SearchFrom < Css.Len())
		{
			const int32 PropIndex = Css.Find(*Prefix, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			if (PropIndex == INDEX_NONE)
			{
				break;
			}
			if (PropIndex > 0)
			{
				const TCHAR Prev = Css[PropIndex - 1];
				if (Prev != TEXT('{') && Prev != TEXT(';') && !FChar::IsWhitespace(Prev))
				{
					SearchFrom = PropIndex + Prefix.Len();
					continue;
				}
			}

			const int32 ValueStart = PropIndex + Prefix.Len();
			const int32 Semi = Css.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
			const int32 Close = Css.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
			const int32 ValueEnd = Semi != INDEX_NONE && (Close == INDEX_NONE || Semi < Close) ? Semi : Close;
			if (ValueEnd == INDEX_NONE)
			{
				break;
			}

			const FString Value = Css.Mid(ValueStart, ValueEnd - ValueStart);
			TArray<FString> Items;
			int32 ItemStart = 0;
			int32 ParenthesisDepth = 0;
			bool bInQuote = false;
			TCHAR QuoteChar = 0;
			for (int32 Index = 0; Index <= Value.Len(); ++Index)
			{
				const bool bAtEnd = Index == Value.Len();
				const TCHAR Ch = bAtEnd ? TEXT(',') : Value[Index];
				if (!bAtEnd && bInQuote)
				{
					if (Ch == QuoteChar && (Index == 0 || Value[Index - 1] != TEXT('\\')))
					{
						bInQuote = false;
					}
				}
				else if (!bAtEnd && (Ch == TEXT('\'') || Ch == TEXT('"')))
				{
					bInQuote = true;
					QuoteChar = Ch;
				}
				else if (!bAtEnd && Ch == TEXT('('))
				{
					++ParenthesisDepth;
				}
				else if (!bAtEnd && Ch == TEXT(')'))
				{
					ParenthesisDepth = FMath::Max(0, ParenthesisDepth - 1);
				}
				else if (Ch == TEXT(',') && ParenthesisDepth == 0)
				{
					FString Item = Value.Mid(ItemStart, Index - ItemStart);
					Item.TrimStartAndEndInline();
					if (!Item.IsEmpty() && !Item.Contains(ValueFragment, ESearchCase::IgnoreCase))
					{
						Items.Add(MoveTemp(Item));
					}
					ItemStart = Index + 1;
				}
			}

			const int32 RemoveEnd = ValueEnd == Semi ? ValueEnd + 1 : ValueEnd;
			const FString Replacement = Items.IsEmpty()
				? FString()
				: Prefix + TEXT(" ") + FString::Join(Items, TEXT(", ")) + TEXT(";");
			Css = Css.Left(PropIndex) + Replacement + Css.Mid(RemoveEnd);
			SearchFrom = PropIndex + Replacement.Len();
		}
	}

	void StripCssAtRuleBlocksContaining(FString& Css, const TCHAR* AtRule, const TCHAR* HeaderFragment)
	{
		int32 SearchFrom = 0;
		while (SearchFrom < Css.Len())
		{
			const int32 RuleIndex = Css.Find(AtRule, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			if (RuleIndex == INDEX_NONE)
			{
				break;
			}
			const int32 BlockOpen = Css.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, RuleIndex);
			if (BlockOpen == INDEX_NONE)
			{
				break;
			}
			const FString Header = Css.Mid(RuleIndex, BlockOpen - RuleIndex);
			if (!Header.Contains(HeaderFragment, ESearchCase::IgnoreCase))
			{
				SearchFrom = BlockOpen + 1;
				continue;
			}

			int32 Depth = 1;
			int32 Cursor = BlockOpen + 1;
			bool bInQuote = false;
			TCHAR QuoteChar = 0;
			while (Cursor < Css.Len() && Depth > 0)
			{
				const TCHAR Ch = Css[Cursor];
				if (bInQuote)
				{
					if (Ch == QuoteChar && Css[Cursor - 1] != TEXT('\\'))
					{
						bInQuote = false;
					}
				}
				else if (Ch == TEXT('\'') || Ch == TEXT('"'))
				{
					bInQuote = true;
					QuoteChar = Ch;
				}
				else if (Ch == TEXT('{'))
				{
					++Depth;
				}
				else if (Ch == TEXT('}'))
				{
					--Depth;
				}
				++Cursor;
			}
			if (Depth != 0)
			{
				break;
			}

			Css.RemoveAt(RuleIndex, Cursor - RuleIndex);
			SearchFrom = RuleIndex;
		}
	}

	void NormalizeBrowserCssDeclarations(FString& Css)
	{
		// Common reset output from browser/Vue builds. RmlUi does not need an equivalent.
		Css.ReplaceInline(TEXT("background: 0 0;"), TEXT(""), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background:0 0;"), TEXT(""), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background:0 0}"), TEXT("}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background: transparent;"), TEXT("background-color: transparent;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background:transparent;"), TEXT("background-color:transparent;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background:transparent}"), TEXT("background-color:transparent}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("min-height: auto;"), TEXT("min-height: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("min-height:auto;"), TEXT("min-height:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("min-height:auto}"), TEXT("min-height:0}"), ESearchCase::IgnoreCase);

		// RmlUi's image element/object replacement handles its own sizing. Ignore CSS object-fit hints
		// instead of letting unsupported declarations poison the rest of a minified rule.
		StripCssDeclarationsByPrefix(Css, TEXT("object-fit:"));
		StripCssDeclarationsByPrefix(Css, TEXT("object-position:"));

		// Browser border shorthands: keep the layout/color intent where possible.
		Css.ReplaceInline(TEXT("border: none;"), TEXT("border-width: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-top: none;"), TEXT("border-top-width: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-right: none;"), TEXT("border-right-width: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-bottom: none;"), TEXT("border-bottom-width: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-left: none;"), TEXT("border-left-width: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border:none;"), TEXT("border-width:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-top:none;"), TEXT("border-top-width:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-right:none;"), TEXT("border-right-width:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-bottom:none;"), TEXT("border-bottom-width:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-left:none;"), TEXT("border-left-width:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border:none}"), TEXT("border-width:0}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-top:none}"), TEXT("border-top-width:0}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-right:none}"), TEXT("border-right-width:0}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-bottom:none}"), TEXT("border-bottom-width:0}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-left:none}"), TEXT("border-left-width:0}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border: 0;"), TEXT("border-width: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-top: 0;"), TEXT("border-top-width: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-right: 0;"), TEXT("border-right-width: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-bottom: 0;"), TEXT("border-bottom-width: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-left: 0;"), TEXT("border-left-width: 0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border:0;"), TEXT("border-width:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-top:0;"), TEXT("border-top-width:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-right:0;"), TEXT("border-right-width:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-bottom:0;"), TEXT("border-bottom-width:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-left:0;"), TEXT("border-left-width:0;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border:0}"), TEXT("border-width:0}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-top:0}"), TEXT("border-top-width:0}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-right:0}"), TEXT("border-right-width:0}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-bottom:0}"), TEXT("border-bottom-width:0}"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("border-left:0}"), TEXT("border-left-width:0}"), ESearchCase::IgnoreCase);

		// Dashed/dotted borders are browser-authored decoration; approximate with solid so RmlUi
		// still keeps the same box size and color instead of rejecting the declaration.
		Css.ReplaceInline(TEXT(" dashed "), TEXT(" solid "), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT(" dotted "), TEXT(" solid "), ESearchCase::IgnoreCase);
	}

	void RewriteGeneratedPseudoSelectors(
		FString& Css,
		TArray<FH5UI_GeneratedPseudoSelector>* GeneratedPseudoSelectors)
	{
		int32 SearchFrom = 0;
		while (SearchFrom < Css.Len())
		{
			const int32 BlockOpen = Css.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (BlockOpen == INDEX_NONE)
			{
				break;
			}

			const FString Prefix = Css.Left(BlockOpen);
			int32 PreviousOpen = INDEX_NONE;
			int32 PreviousClose = INDEX_NONE;
			Prefix.FindLastChar(TEXT('{'), PreviousOpen);
			Prefix.FindLastChar(TEXT('}'), PreviousClose);
			const int32 HeaderStart = FMath::Max(PreviousOpen, PreviousClose) + 1;
			const FString Header = Css.Mid(HeaderStart, BlockOpen - HeaderStart);
			if (!Header.Contains(TEXT("::before"), ESearchCase::IgnoreCase) &&
				!Header.Contains(TEXT("::after"), ESearchCase::IgnoreCase))
			{
				SearchFrom = BlockOpen + 1;
				continue;
			}

			TArray<FString> Selectors;
			Header.ParseIntoArray(Selectors, TEXT(","), false);
			for (FString& Selector : Selectors)
			{
				Selector.TrimStartAndEndInline();
				const bool bBefore = Selector.Contains(TEXT("::before"), ESearchCase::IgnoreCase);
				const bool bAfter = Selector.Contains(TEXT("::after"), ESearchCase::IgnoreCase);
				if (!bBefore && !bAfter)
				{
					continue;
				}

				FString BaseSelector = Selector;
				BaseSelector.ReplaceInline(TEXT("::before"), TEXT(""), ESearchCase::IgnoreCase);
				BaseSelector.ReplaceInline(TEXT("::after"), TEXT(""), ESearchCase::IgnoreCase);
				BaseSelector.TrimStartAndEndInline();
				if (BaseSelector.IsEmpty() || BaseSelector.StartsWith(TEXT("@")))
				{
					continue;
				}

				const FH5UI_GeneratedPseudoSelector Rule{BaseSelector, bBefore};
				if (GeneratedPseudoSelectors)
				{
					GeneratedPseudoSelectors->AddUnique(Rule);
				}
				Selector = BaseSelector + (bBefore
					? TEXT(" > .h5ui-generated-before")
					: TEXT(" > .h5ui-generated-after"));
			}

			const FString RewrittenHeader = FString::Join(Selectors, TEXT(", "));
			Css = Css.Left(HeaderStart) + RewrittenHeader + Css.Mid(BlockOpen);
			SearchFrom = HeaderStart + RewrittenHeader.Len() + 1;
		}

		// Generated text is not supported yet. Empty content still creates the
		// browser-compatible decorative box represented by the generated element.
		StripCssDeclarationsByPrefix(Css, TEXT("content:"));
	}

	void NormalizeCssForRml(FString& Css, TArray<FH5UI_GeneratedPseudoSelector>* GeneratedPseudoSelectors)
	{
		// This browser preference query has no meaningful native equivalent and its
		// nested declarations are rejected by RmlUi. Remove the complete block before
		// selector/pseudo-element rewriting sees it.
		StripCssAtRuleBlocksContaining(Css, TEXT("@media"), TEXT("prefers-reduced-motion"));

		RewriteGeneratedPseudoSelectors(Css, GeneratedPseudoSelectors);
		// Multi-line browser CSS: collapse function bodies so RmlUi's property scanners work.
		CollapseCssFunctionBodies(Css, TEXT("linear-gradient"));
		CollapseCssFunctionBodies(Css, TEXT("radial-gradient"));
		CollapseCssFunctionBodies(Css, TEXT("repeating-linear-gradient"));
		CollapseCssFunctionBodies(Css, TEXT("calc"));
		CollapseCssFunctionBodies(Css, TEXT("rgba"));
		CollapseCssFunctionBodies(Css, TEXT("hsla"));
		CollapseCssFunctionBodies(Css, TEXT("rgb"));
		CollapseCssFunctionBodies(Css, TEXT("hsl"));

		FoldTrivialCssCalc(Css);
		NormalizeBrowserCssDeclarations(Css);
		Css.ReplaceInline(TEXT("!important"), TEXT(""), ESearchCase::IgnoreCase);

		// Browser keyword origins are normalized to the percentage form accepted by
		// the native transform parser.
		Css.ReplaceInline(TEXT("transform-origin: top left;"), TEXT("transform-origin: 0% 0%;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("transform-origin: top right;"), TEXT("transform-origin: 100% 0%;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("transform-origin: bottom left;"), TEXT("transform-origin: 0% 100%;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("transform-origin: bottom right;"), TEXT("transform-origin: 100% 100%;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("transform-origin: center;"), TEXT("transform-origin: 50% 50%;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("transform-origin: bottom center;"), TEXT("transform-origin: 50% 100%;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("transform-origin: left center;"), TEXT("transform-origin: 0% 50%;"), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("transform-origin: right center;"), TEXT("transform-origin: 100% 50%;"), ESearchCase::IgnoreCase);

		// Native scrollbars are styled by the renderer. Browser writing modes and
		// scrollbar properties currently fail declaration parsing.
		StripCssDeclarationsByPrefix(Css, TEXT("writing-mode:"));
		StripCssDeclarationsByPrefix(Css, TEXT("scrollbar-color:"));
		StripCssDeclarationsByPrefix(Css, TEXT("scrollbar-width:"));

		// RmlUi cannot interpolate box shadows. More importantly, a stylesheet that
		// combines an infinite animation with box-shadow keyframes can monopolize the
		// game/editor thread. Prefer a static shadow over risking an unbounded update.
		StripCssListItemsContainingValue(Css, TEXT("transition"), TEXT("box-shadow"));
		if (Css.Contains(TEXT("@keyframes"), ESearchCase::IgnoreCase) &&
			Css.Contains(TEXT("box-shadow:"), ESearchCase::IgnoreCase))
		{
			StripCssDeclarationsByPrefix(Css, TEXT("animation:"));
			StripCssDeclarationsByPrefix(Css, TEXT("animation-name:"));
			StripCssDeclarationsByPrefix(Css, TEXT("animation-duration:"));
			StripCssDeclarationsByPrefix(Css, TEXT("animation-delay:"));
			StripCssDeclarationsByPrefix(Css, TEXT("animation-iteration-count:"));
			StripCssDeclarationsByPrefix(Css, TEXT("animation-direction:"));
			StripCssDeclarationsByPrefix(Css, TEXT("animation-timing-function:"));
			StripCssDeclarationsByPrefix(Css, TEXT("animation-fill-mode:"));
			StripCssDeclarationsByPrefix(Css, TEXT("animation-play-state:"));
		}

		// RmlUi treats the browser-wide `inherit` keyword as a literal string for
		// font declarations. Let its normal inherited-property cascade do the work
		// by removing those declarations before parsing.
		StripCssDeclarationWithValue(Css, TEXT("color"), TEXT("inherit"));
		StripCssDeclarationWithValue(Css, TEXT("font"), TEXT("inherit"));
		StripCssDeclarationWithValue(Css, TEXT("font-family"), TEXT("inherit"));
		StripCssDeclarationWithValue(Css, TEXT("font-size"), TEXT("inherit"));
		StripCssDeclarationWithValue(Css, TEXT("font-style"), TEXT("inherit"));
		StripCssDeclarationWithValue(Css, TEXT("font-weight"), TEXT("inherit"));

		// Browser background image / gradient spelling → RmlUi decorator
		Css.ReplaceInline(TEXT("background-image: url("), TEXT("decorator: image("), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-image: linear-gradient("), TEXT("decorator: linear-gradient("), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-image: radial-gradient("), TEXT("decorator: radial-gradient("), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-image: repeating-linear-gradient("), TEXT("decorator: linear-gradient("), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-image: none;"), TEXT(""), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background: linear-gradient("), TEXT("decorator: linear-gradient("), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background: radial-gradient("), TEXT("decorator: radial-gradient("), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background: url("), TEXT("decorator: image("), ESearchCase::IgnoreCase);

		// RmlUi image decorator fills the paint area; discard browser-only longhands.
		Css.ReplaceInline(TEXT("background-size: cover;"), TEXT(""), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-size: contain;"), TEXT(""), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-size: 100% 100%;"), TEXT(""), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-size: auto;"), TEXT(""), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-position: center center;"), TEXT(""), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-position: center;"), TEXT(""), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-repeat: no-repeat;"), TEXT(""), ESearchCase::IgnoreCase);
		Css.ReplaceInline(TEXT("background-repeat: repeat;"), TEXT(""), ESearchCase::IgnoreCase);

		// Browser-only / non-layout noise that can break declaration scanning if combined oddly.
		StripCssDeclarationsByPrefix(Css, TEXT("outline:"));
		StripCssDeclarationsByPrefix(Css, TEXT("outline-offset:"));
		StripCssDeclarationsByPrefix(Css, TEXT("user-select:"));
		StripCssDeclarationsByPrefix(Css, TEXT("-webkit-user-select:"));
		StripCssDeclarationsByPrefix(Css, TEXT("-moz-user-select:"));
		StripCssDeclarationsByPrefix(Css, TEXT("appearance:"));
		StripCssDeclarationsByPrefix(Css, TEXT("-webkit-appearance:"));
		StripCssDeclarationsByPrefix(Css, TEXT("caret-color:"));
		StripCssDeclarationsByPrefix(Css, TEXT("touch-action:"));
		StripCssDeclarationsByPrefix(Css, TEXT("will-change:"));
		StripCssDeclarationsByPrefix(Css, TEXT("backdrop-filter:"));
		// backdrop-filter needs layer RT; strip so the rest of the rule still applies.
		StripCssDeclarationsByPrefix(Css, TEXT("filter:"));
		// Full CSS filter stack needs SupportsLayerRendering; keep pages working without CEF.
	}
}

double FH5UI_SystemInterface::GetElapsedTime()
{
	return FPlatformTime::Seconds();
}

void FH5UI_SystemInterface::JoinPath(
	Rml::String& TranslatedPath,
	const Rml::String& DocumentPath,
	const Rml::String& Path)
{
	FString RequestedPath = H5UIPlugin::FromUtf8(Path);
	RequestedPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	int32 ColonIndex = INDEX_NONE;
	int32 SlashIndex = INDEX_NONE;
	RequestedPath.FindChar(TEXT(':'), ColonIndex);
	RequestedPath.FindChar(TEXT('/'), SlashIndex);
	if (ColonIndex != INDEX_NONE && (SlashIndex == INDEX_NONE || ColonIndex < SlashIndex))
	{
		TranslatedPath = Path;
		return;
	}

	FString BasePath = H5UIPlugin::StripUrlQueryAndFragment(H5UIPlugin::FromUtf8(DocumentPath));
	const int32 SchemeIndex = BasePath.Find(TEXT("://"));
	if (SchemeIndex != INDEX_NONE)
	{
		if (RequestedPath.StartsWith(TEXT("/")))
		{
			const int32 AuthorityEnd = BasePath.Find(
				TEXT("/"),
				ESearchCase::CaseSensitive,
				ESearchDir::FromStart,
				SchemeIndex + 3);
			BasePath = AuthorityEnd == INDEX_NONE ? BasePath : BasePath.Left(AuthorityEnd);
		}
		else
		{
			int32 LastSlash = INDEX_NONE;
			if (BasePath.FindLastChar(TEXT('/'), LastSlash))
			{
				BasePath.LeftInline(LastSlash + 1);
			}
			else
			{
				BasePath.AppendChar(TEXT('/'));
			}
		}

		TranslatedPath = H5UIPlugin::ToUtf8(BasePath + RequestedPath);
		return;
	}

	Rml::SystemInterface::JoinPath(TranslatedPath, DocumentPath, Path);
}

bool FH5UI_SystemInterface::LogMessage(Rml::Log::Type Type, const Rml::String& Message)
{
	const FString Text = H5UIPlugin::FromUtf8(Message);
	switch (Type)
	{
	case Rml::Log::LT_ERROR:
	case Rml::Log::LT_ASSERT:
		UE_LOG(LogH5UIPluginRuntime, Error, TEXT("%s"), *Text);
		break;
	case Rml::Log::LT_WARNING:
		UE_LOG(LogH5UIPluginRuntime, Warning, TEXT("%s"), *Text);
		break;
	case Rml::Log::LT_INFO:
		UE_LOG(LogH5UIPluginRuntime, Log, TEXT("%s"), *Text);
		break;
	default:
		UE_LOG(LogH5UIPluginRuntime, Verbose, TEXT("%s"), *Text);
		break;
	}
	return true;
}

void FH5UI_SystemInterface::SetClipboardText(const Rml::String& Text)
{
	FPlatformApplicationMisc::ClipboardCopy(*H5UIPlugin::FromUtf8(Text));
}

void FH5UI_SystemInterface::GetClipboardText(Rml::String& Text)
{
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);
	Text = H5UIPlugin::ToUtf8(ClipboardText);
}

void FH5UI_FileInterface::SetPluginResourceRoot(const FString& InRoot)
{
	PluginResourceRoot = FPaths::ConvertRelativePathToFull(InRoot);
	FPaths::NormalizeDirectoryName(PluginResourceRoot);
}

FString FH5UI_FileInterface::ResolvePath(const FString& Path) const
{
	FString Resolved = H5UIPlugin::StripUrlQueryAndFragment(Path);
	Resolved.ReplaceInline(TEXT("\\"), TEXT("/"));
	FString AllowedRoot;

	const FString CouiPrefix(TEXT("coui://uiresources/"));
	const FString H5PluginPrefix(TEXT("h5ui://plugin/"));
	const FString LegacyPluginPrefix(TEXT("silver://plugin/"));
	const FString FilePrefix(TEXT("file:///"));

	if (Resolved.StartsWith(CouiPrefix, ESearchCase::IgnoreCase))
	{
		const FString RelativePath = Resolved.RightChop(CouiPrefix.Len());
		const FString ResourceRoot = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectContentDir(), GetDefault<UH5UI_Settings>()->ResourceDirectory);
		AllowedRoot = ResourceRoot;
		Resolved = FPaths::Combine(ResourceRoot, RelativePath);
	}
	else if (Resolved.StartsWith(H5PluginPrefix, ESearchCase::IgnoreCase))
	{
		AllowedRoot = PluginResourceRoot;
		Resolved = FPaths::Combine(PluginResourceRoot, Resolved.RightChop(H5PluginPrefix.Len()));
	}
	else if (Resolved.StartsWith(LegacyPluginPrefix, ESearchCase::IgnoreCase))
	{
		// Keep serialized widget assets created before the plugin rename loadable.
		AllowedRoot = PluginResourceRoot;
		Resolved = FPaths::Combine(PluginResourceRoot, Resolved.RightChop(LegacyPluginPrefix.Len()));
	}
	else if (Resolved.StartsWith(FilePrefix, ESearchCase::IgnoreCase))
	{
		if (!GetDefault<UH5UI_Settings>()->bAllowAbsoluteFilePaths)
		{
			return FString();
		}
		Resolved = Resolved.RightChop(FilePrefix.Len());
	}
	else if (FPaths::IsRelative(Resolved))
	{
		AllowedRoot = FPaths::Combine(
			FPaths::ProjectContentDir(),
			GetDefault<UH5UI_Settings>()->ResourceDirectory);
		Resolved = FPaths::Combine(AllowedRoot, Resolved);
	}
	else if (!GetDefault<UH5UI_Settings>()->bAllowAbsoluteFilePaths)
	{
		return FString();
	}

	FPaths::CollapseRelativeDirectories(Resolved);
	FPaths::NormalizeFilename(Resolved);
	Resolved = FPaths::ConvertRelativePathToFull(Resolved);

	if (!AllowedRoot.IsEmpty())
	{
		FPaths::CollapseRelativeDirectories(AllowedRoot);
		FPaths::NormalizeDirectoryName(AllowedRoot);
		AllowedRoot = FPaths::ConvertRelativePathToFull(AllowedRoot);
		if (!FPaths::IsSamePath(Resolved, AllowedRoot) && !FPaths::IsUnderDirectory(Resolved, AllowedRoot))
		{
			UE_LOG(LogH5UIPluginRuntime, Warning, TEXT("Blocked UI resource outside its root: %s"), *Path);
			return FString();
		}
	}

	return Resolved;
}

Rml::FileHandle FH5UI_FileInterface::Open(const Rml::String& Path)
{
	const FString ResolvedPath = ResolvePath(H5UIPlugin::FromUtf8(Path));
	if (ResolvedPath.IsEmpty())
	{
		return 0;
	}

	TUniquePtr<FOpenFile> OpenFile = MakeUnique<FOpenFile>();
	if (ResolvedPath.EndsWith(TEXT(".css"), ESearchCase::IgnoreCase))
	{
		FString Css;
		if (!FFileHelper::LoadFileToString(Css, *ResolvedPath))
		{
			return 0;
		}

		H5UIPlugin::NormalizeCssForRml(
			Css,
			bCaptureGeneratedPseudoSelectors ? &CapturedGeneratedPseudoSelectors : nullptr);
		FTCHARToUTF8 Utf8(*Css);
		OpenFile->Memory.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
	else
	{
		OpenFile->DiskFile.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*ResolvedPath));
		if (!OpenFile->DiskFile)
		{
			return 0;
		}
	}

	return reinterpret_cast<Rml::FileHandle>(OpenFile.Release());
}

void FH5UI_FileInterface::Close(Rml::FileHandle File)
{
	delete reinterpret_cast<FOpenFile*>(File);
}

size_t FH5UI_FileInterface::Read(void* Buffer, size_t Size, Rml::FileHandle File)
{
	FOpenFile* OpenFile = reinterpret_cast<FOpenFile*>(File);
	if (!OpenFile || Size == 0)
	{
		return 0;
	}
	if (OpenFile->Memory.Num() > 0)
	{
		const int64 Remaining = FMath::Max<int64>(0, OpenFile->Memory.Num() - OpenFile->Offset);
		const int64 BytesToRead = FMath::Min<int64>(static_cast<int64>(Size), Remaining);
		if (BytesToRead > 0)
		{
			FMemory::Memcpy(Buffer, OpenFile->Memory.GetData() + OpenFile->Offset, BytesToRead);
			OpenFile->Offset += BytesToRead;
		}
		return static_cast<size_t>(BytesToRead);
	}

	IFileHandle* Handle = OpenFile->DiskFile.Get();
	if (!Handle)
	{
		return 0;
	}
	const int64 Remaining = FMath::Max<int64>(0, Handle->Size() - Handle->Tell());
	const int64 BytesToRead = FMath::Min<int64>(static_cast<int64>(Size), Remaining);
	return BytesToRead > 0 && Handle->Read(static_cast<uint8*>(Buffer), BytesToRead)
		? static_cast<size_t>(BytesToRead)
		: 0;
}

bool FH5UI_FileInterface::Seek(Rml::FileHandle File, long Offset, int Origin)
{
	FOpenFile* OpenFile = reinterpret_cast<FOpenFile*>(File);
	if (!OpenFile)
	{
		return false;
	}
	if (OpenFile->Memory.Num() > 0)
	{
		const int64 BaseOffset = Origin == SEEK_SET ? 0 : Origin == SEEK_CUR ? OpenFile->Offset : OpenFile->Memory.Num();
		OpenFile->Offset = FMath::Clamp<int64>(BaseOffset + Offset, 0, OpenFile->Memory.Num());
		return true;
	}

	IFileHandle* Handle = OpenFile->DiskFile.Get();
	if (!Handle)
	{
		return false;
	}

	switch (Origin)
	{
	case SEEK_SET:
		return Handle->Seek(Offset);
	case SEEK_CUR:
		return Handle->Seek(Handle->Tell() + Offset);
	case SEEK_END:
		return Handle->SeekFromEnd(Offset);
	default:
		return false;
	}
}

size_t FH5UI_FileInterface::Tell(Rml::FileHandle File)
{
	FOpenFile* OpenFile = reinterpret_cast<FOpenFile*>(File);
	if (!OpenFile)
	{
		return 0;
	}
	return OpenFile->Memory.Num() > 0
		? static_cast<size_t>(OpenFile->Offset)
		: OpenFile->DiskFile ? static_cast<size_t>(OpenFile->DiskFile->Tell()) : 0;
}

size_t FH5UI_FileInterface::Length(Rml::FileHandle File)
{
	FOpenFile* OpenFile = reinterpret_cast<FOpenFile*>(File);
	if (!OpenFile)
	{
		return 0;
	}
	return OpenFile->Memory.Num() > 0
		? static_cast<size_t>(OpenFile->Memory.Num())
		: OpenFile->DiskFile ? static_cast<size_t>(OpenFile->DiskFile->Size()) : 0;
}

struct FH5UI_RenderInterface::FCompiledGeometry
{
	TArray<Rml::Vertex> Vertices;
	TArray<SlateIndex> Indices;
};

struct FH5UI_RenderInterface::FCompiledShader
{
	Rml::Vector2f GradientStart = Rml::Vector2f(0.0f, 0.0f);
	Rml::Vector2f GradientEnd = Rml::Vector2f(0.0f, 1.0f);
	bool bRepeating = false;
	Rml::ColorStopList ColorStops;
};

struct FH5UI_RenderInterface::FTextureData
{
	TSharedPtr<FSlateBrush> Brush;
	// Package assets may be kept alive by the renderer, but PIE/world-owned runtime
	// resources must stay weak or they prevent the old PIE world from being GC'd.
	TStrongObjectPtr<UObject> OwnedResourceObject;
	TWeakObjectPtr<UObject> ExternalResourceObject;
	FIntPoint Size = FIntPoint::ZeroValue;
	bool bExternalResource = false;
	bool bPremultipliedAlpha = true;
};

struct FH5UI_RenderInterface::FScopedSlateVertices
{
	// MakeCustomVerts copies into its draw element before returning. Keep the
	// shared buffer borrowed through submission; reentrant draws use a local one.
	FScopedSlateVertices(FH5UI_RenderInterface& InRenderer, int32 VertexCount)
		: Renderer(InRenderer)
		, bUsesScratch(!Renderer.bSlateVertexScratchInUse)
		, Vertices(bUsesScratch ? Renderer.SlateVertexScratch : FallbackVertices)
	{
		if (bUsesScratch)
		{
			Renderer.bSlateVertexScratchInUse = true;
		}
		Vertices.Reset();
#if WITH_DEV_AUTOMATION_TESTS
		const int32 PreviousCapacity = Vertices.Max();
#endif
		Vertices.Reserve(VertexCount);
#if WITH_DEV_AUTOMATION_TESTS
		if (bUsesScratch && Vertices.Max() > PreviousCapacity)
		{
			++Renderer.SlateVertexScratchGrowthCount;
		}
#endif
	}

	~FScopedSlateVertices()
	{
		if (bUsesScratch)
		{
			Renderer.bSlateVertexScratchInUse = false;
		}
	}

	FScopedSlateVertices(const FScopedSlateVertices&) = delete;
	FScopedSlateVertices& operator=(const FScopedSlateVertices&) = delete;

	FH5UI_RenderInterface& Renderer;
	const bool bUsesScratch;
	TArray<FSlateVertex> FallbackVertices;
	TArray<FSlateVertex>& Vertices;
};

FH5UI_RenderInterface::FH5UI_RenderInterface() = default;
FH5UI_RenderInterface::~FH5UI_RenderInterface() = default;

void FH5UI_RenderInterface::SetFileInterface(FH5UI_FileInterface* InFileInterface)
{
	FileInterface = InFileInterface;
}

void FH5UI_RenderInterface::BeginPaint(
	const FGeometry& InGeometry,
	FSlateWindowElementList& InElementList,
	int32 InLayerId,
	float InCoordinateScale)
{
	check(!ElementList);
	PaintGeometry = &InGeometry;
	ElementList = &InElementList;
	LayerId = InLayerId;
	CoordinateScale = FMath::Max(InCoordinateScale, KINDA_SMALL_NUMBER);
	bSlateScissorClipPushed = false;
	bSlateMaskClipPushed = false;
	CurrentStats = FH5UI_PerformanceStats();
	PaintStartSeconds = FPlatformTime::Seconds();
	RefreshSlateClips();
}

FH5UI_PerformanceStats FH5UI_RenderInterface::EndPaint(const FIntPoint& ViewSize)
{
	if (bSlateMaskClipPushed && ElementList)
	{
		ElementList->PopClip();
	}
	if (bSlateScissorClipPushed && ElementList)
	{
		ElementList->PopClip();
	}

	CurrentStats.SubmitMilliseconds = static_cast<float>(
		(FPlatformTime::Seconds() - PaintStartSeconds) * 1000.0);
	CurrentStats.ViewSize = ViewSize;

	bSlateMaskClipPushed = false;
	bSlateScissorClipPushed = false;
	PaintGeometry = nullptr;
	ElementList = nullptr;
	return CurrentStats;
}

Rml::TextureHandle FH5UI_RenderInterface::CreateExternalTexture(UTexture* Texture)
{
	if (!Texture)
	{
		return 0;
	}

	TUniquePtr<FTextureData> TextureData = MakeUnique<FTextureData>();
	TextureData->ExternalResourceObject = Texture;
	TextureData->bExternalResource = true;
	TextureData->bPremultipliedAlpha = false;
	// Only cooked/editor content assets may be rooted by an HTML texture handle.
	// A runtime render target can have a world actor/component in its Outer chain;
	// rooting it here would keep the entire PIE world alive after EndPlay.
	if (Texture->IsAsset())
	{
		TextureData->OwnedResourceObject = TStrongObjectPtr<UObject>(Texture);
	}
	TextureData->Brush = MakeShared<FSlateBrush>();
	TextureData->Brush->SetResourceObject(Texture);
	TextureData->Brush->ImageType = ESlateBrushImageType::FullColor;
	TextureData->Brush->DrawAs = ESlateBrushDrawType::Image;
	TextureData->Size = FIntPoint(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight());
	TextureData->Brush->SetImageSize(FVector2D(TextureData->Size.X, TextureData->Size.Y));
	return reinterpret_cast<Rml::TextureHandle>(TextureData.Release());
}

Rml::CompiledGeometryHandle FH5UI_RenderInterface::CompileGeometry(
	Rml::Span<const Rml::Vertex> Vertices,
	Rml::Span<const int> Indices)
{
	TUniquePtr<FCompiledGeometry> Geometry = MakeUnique<FCompiledGeometry>();
	Geometry->Vertices.Append(Vertices.data(), static_cast<int32>(Vertices.size()));
	Geometry->Indices.Reserve(static_cast<int32>(Indices.size()));

	for (const int Index : Indices)
	{
		if (Index < 0 || static_cast<uint64>(Index) > static_cast<uint64>(TNumericLimits<SlateIndex>::Max()))
		{
			return 0;
		}
		Geometry->Indices.Add(static_cast<SlateIndex>(Index));
	}

	return reinterpret_cast<Rml::CompiledGeometryHandle>(Geometry.Release());
}

void FH5UI_RenderInterface::RenderGeometry(
	Rml::CompiledGeometryHandle GeometryHandle,
	Rml::Vector2f Translation,
	Rml::TextureHandle TextureHandle)
{
	if (!ElementList || !PaintGeometry || !GeometryHandle)
	{
		return;
	}

	const FCompiledGeometry* Geometry = reinterpret_cast<FCompiledGeometry*>(GeometryHandle);
	const FTextureData* TextureData = reinterpret_cast<FTextureData*>(TextureHandle);
	if (TextureData && TextureData->bExternalResource && !TextureData->ExternalResourceObject.IsValid())
	{
		return;
	}
	const FSlateRenderTransform& SlateTransform = PaintGeometry->GetAccumulatedRenderTransform();
	const TOptional<FSlateRect> ActiveClipRect = GetActiveLocalClipRect();
	auto TransformPosition = [this, Translation, ActiveClipRect](const Rml::Vertex& Vertex)
	{
		Rml::Vector4f Position(Vertex.position.x + Translation.x, Vertex.position.y + Translation.y, 0.0f, 1.0f);
		if (CurrentTransform.IsSet())
		{
			Position = CurrentTransform.GetValue() * Position;
		}

		const float W = FMath::IsNearlyZero(Position.w) ? 1.0f : Position.w;
		FVector2f Result = FVector2f(Position.x / W, Position.y / W) * CoordinateScale;
		if (ActiveClipRect.IsSet())
		{
			const FSlateRect Clip = ActiveClipRect.GetValue();
			const FVector2f Unclipped = Result;
			Result.X = FMath::Clamp(Unclipped.X, Clip.Left, Clip.Right);
			Result.Y = FMath::Clamp(Unclipped.Y, Clip.Top, Clip.Bottom);
			if (!Result.Equals(Unclipped, KINDA_SMALL_NUMBER))
			{
				++CurrentStats.ClampedVertices;
			}
		}
		return Result;
	};

	// Slate's custom-triangle path can drop a translucent one-pixel quad on one axis,
	// even though RmlUi has laid it out correctly. This is most visible in CSS grids
	// built from absolutely positioned 1px elements. Route simple, untextured thin
	// rectangles through Slate's native line primitive so horizontal and vertical
	// rules rasterize consistently at every viewport scale.
	if (!TextureHandle && Geometry->Vertices.Num() == 4 && Geometry->Indices.Num() == 6)
	{
		const float MaximumFloat = TNumericLimits<float>::Max();
		FVector2f Min(MaximumFloat, MaximumFloat);
		FVector2f Max(-MaximumFloat, -MaximumFloat);
		bool bUniformColor = true;
		const Rml::ColourbPremultiplied LineColor = Geometry->Vertices[0].colour;
		for (const Rml::Vertex& Vertex : Geometry->Vertices)
		{
			const FVector2f Position = TransformPosition(Vertex);
			Min.X = FMath::Min(Min.X, Position.X);
			Min.Y = FMath::Min(Min.Y, Position.Y);
			Max.X = FMath::Max(Max.X, Position.X);
			Max.Y = FMath::Max(Max.Y, Position.Y);
			bUniformColor &= Vertex.colour == LineColor;
		}

		const FVector2f Size = Max - Min;
		// Cover 1px–2px CSS rules and sub-pixel AA cases (CommanderOS uses many 1px rgba lines).
		const bool bHorizontalRule = Size.X > 1.01f && Size.Y > 0.0f && Size.Y <= 1.01f;
		const bool bVerticalRule = Size.Y > 1.01f && Size.X > 0.0f && Size.X <= 1.01f;
		if (bUniformColor && LineColor.alpha > 0 && (bHorizontalRule || bVerticalRule))
		{
			const float Alpha = float(LineColor.alpha) / 255.0f;
			const float InversePremultipliedAlpha = Alpha > 0.0f ? 1.0f / (255.0f * Alpha) : 0.0f;
			const FLinearColor Tint(
				float(LineColor.red) * InversePremultipliedAlpha,
				float(LineColor.green) * InversePremultipliedAlpha,
				float(LineColor.blue) * InversePremultipliedAlpha,
				Alpha);
			TArray<FVector2f> LinePoints;
			LinePoints.Reserve(2);
			if (bHorizontalRule)
			{
				const float CenterY = 0.5f * (Min.Y + Max.Y);
				LinePoints.Add(FVector2f(Min.X, CenterY));
				LinePoints.Add(FVector2f(Max.X, CenterY));
			}
			else
			{
				const float CenterX = 0.5f * (Min.X + Max.X);
				LinePoints.Add(FVector2f(CenterX, Min.Y));
				LinePoints.Add(FVector2f(CenterX, Max.Y));
			}

			FSlateDrawElement::MakeLines(
				*ElementList,
				LayerId++,
				PaintGeometry->ToPaintGeometry(),
				MoveTemp(LinePoints),
				ESlateDrawEffect::None,
				Tint,
				false,
				bHorizontalRule ? Size.Y : Size.X);
			++CurrentStats.DrawBatches;
			CurrentStats.Vertices += 4;
			CurrentStats.Triangles += 2;
			return;
		}
	}

	const FSlateResourceHandle ResourceHandle = ResolveResourceHandle(reinterpret_cast<FTextureData*>(TextureHandle));
	FScopedSlateVertices VertexBuffer(*this, Geometry->Vertices.Num());
	TArray<FSlateVertex>& SlateVertices = VertexBuffer.Vertices;
	for (const Rml::Vertex& Vertex : Geometry->Vertices)
	{
		const FVector2f LocalPosition = TransformPosition(Vertex);
		const FColor Color(Vertex.colour.red, Vertex.colour.green, Vertex.colour.blue, Vertex.colour.alpha);
		SlateVertices.Add(FSlateVertex::Make(
			SlateTransform,
			LocalPosition,
			FVector2f(Vertex.tex_coord.x, Vertex.tex_coord.y),
			Color));
	}

	FSlateDrawElement::MakeCustomVerts(
		*ElementList,
		LayerId++,
		ResourceHandle,
		SlateVertices,
		Geometry->Indices,
		nullptr,
		0,
		0,
		TextureData && !TextureData->bPremultipliedAlpha
			? ESlateDrawEffect::None
			: ESlateDrawEffect::PreMultipliedAlpha);

	++CurrentStats.DrawBatches;
	CurrentStats.Vertices += SlateVertices.Num();
	CurrentStats.Triangles += Geometry->Indices.Num() / 3;
}

void FH5UI_RenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle Geometry)
{
	delete reinterpret_cast<FCompiledGeometry*>(Geometry);
}

Rml::TextureHandle FH5UI_RenderInterface::LoadTexture(
	Rml::Vector2i& TextureDimensions,
	const Rml::String& Source)
{
	const FString SourceUrl = H5UIPlugin::StripUrlQueryAndFragment(H5UIPlugin::FromUtf8(Source));
	const FString AssetPrefix(TEXT("ueasset://"));
	if (SourceUrl.StartsWith(AssetPrefix, ESearchCase::IgnoreCase))
	{
		FString AssetPath = SourceUrl.RightChop(AssetPrefix.Len());
		if (!AssetPath.StartsWith(TEXT("/")))
		{
			AssetPath.InsertAt(0, TEXT('/'));
		}

		// Runtime render targets are already loaded transient objects and cannot be
		// loaded from a package. Resolve them first, then fall back to asset loading.
		UTexture* Texture = FindObject<UTexture>(nullptr, *AssetPath);
		if (!Texture)
		{
			Texture = LoadObject<UTexture>(nullptr, *AssetPath);
		}
		if (!Texture)
		{
			UE_LOG(LogH5UIPluginRuntime, Warning, TEXT("Unable to load Unreal texture asset: %s"), *AssetPath);
			return 0;
		}

		TextureDimensions = Rml::Vector2i(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight());
		return CreateExternalTexture(Texture);
	}

	if (!FileInterface)
	{
		return 0;
	}

	const FString ResolvedPath = FileInterface->ResolvePath(H5UIPlugin::FromUtf8(Source));
	if (FPaths::GetExtension(ResolvedPath).Equals(TEXT("svg"), ESearchCase::IgnoreCase))
	{
		FString Svg;
		if (ResolvedPath.IsEmpty() || !FFileHelper::LoadFileToString(Svg, *ResolvedPath))
		{
			UE_LOG(LogH5UIPluginRuntime, Warning, TEXT("Unable to load SVG UI texture: %s"), *ResolvedPath);
			return 0;
		}

		FTCHARToUTF8 SvgUtf8(*Svg);
		TArray<ANSICHAR> SvgBuffer;
		SvgBuffer.Append(SvgUtf8.Get(), SvgUtf8.Length());
		SvgBuffer.Add('\0');
		NSVGimage* SvgImage = nsvgParse(SvgBuffer.GetData(), "px", 96.0f);
		if (!SvgImage || SvgImage->width <= 0.0f || SvgImage->height <= 0.0f)
		{
			if (SvgImage)
			{
				nsvgDelete(SvgImage);
			}
			UE_LOG(LogH5UIPluginRuntime, Warning, TEXT("Unable to parse SVG UI texture: %s"), *ResolvedPath);
			return 0;
		}

		const float Width = FMath::Max(1.0f, SvgImage->width);
		const float Height = FMath::Max(1.0f, SvgImage->height);
		const float RasterScale = FMath::Min(4.0f, 2048.0f / FMath::Max(Width, Height));
		const int32 RasterWidth = FMath::Max(1, FMath::CeilToInt(Width * RasterScale));
		const int32 RasterHeight = FMath::Max(1, FMath::CeilToInt(Height * RasterScale));
		TArray<uint8> Pixels;
		Pixels.SetNumZeroed(RasterWidth * RasterHeight * 4);
		NSVGrasterizer* Rasterizer = nsvgCreateRasterizer();
		if (!Rasterizer)
		{
			nsvgDelete(SvgImage);
			UE_LOG(LogH5UIPluginRuntime, Warning, TEXT("Unable to create SVG rasterizer: %s"), *ResolvedPath);
			return 0;
		}

		nsvgRasterize(
			Rasterizer,
			SvgImage,
			0.0f,
			0.0f,
			RasterScale,
			Pixels.GetData(),
			RasterWidth,
			RasterHeight,
			RasterWidth * 4);
		nsvgDeleteRasterizer(Rasterizer);
		nsvgDelete(SvgImage);

		TextureDimensions = Rml::Vector2i(FMath::RoundToInt(Width), FMath::RoundToInt(Height));
		return CreateTexture(Pixels, RasterWidth, RasterHeight);
	}

	TArray64<uint8> CompressedData;
	if (ResolvedPath.IsEmpty() || !FFileHelper::LoadFileToArray(CompressedData, *ResolvedPath))
	{
		UE_LOG(LogH5UIPluginRuntime, Warning, TEXT("Unable to load UI texture: %s"), *ResolvedPath);
		return 0;
	}

	IImageWrapperModule& ImageWrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	FImage Image;
	if (!ImageWrapper.DecompressImage(CompressedData.GetData(), CompressedData.Num(), Image))
	{
		UE_LOG(LogH5UIPluginRuntime, Warning, TEXT("Unsupported UI texture: %s"), *ResolvedPath);
		return 0;
	}

	Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	TArray<uint8> Pixels;
	Pixels.Append(Image.RawData.GetData(), static_cast<int32>(Image.RawData.Num()));

	for (int32 Index = 0; Index + 3 < Pixels.Num(); Index += 4)
	{
		const uint32 Alpha = Pixels[Index + 3];
		Pixels[Index] = static_cast<uint8>((Pixels[Index] * Alpha) / 255);
		Pixels[Index + 1] = static_cast<uint8>((Pixels[Index + 1] * Alpha) / 255);
		Pixels[Index + 2] = static_cast<uint8>((Pixels[Index + 2] * Alpha) / 255);
	}

	TextureDimensions = Rml::Vector2i(Image.SizeX, Image.SizeY);
	return CreateTexture(Pixels, Image.SizeX, Image.SizeY);
}

Rml::TextureHandle FH5UI_RenderInterface::GenerateTexture(
	Rml::Span<const Rml::byte> Source,
	Rml::Vector2i SourceDimensions)
{
	const int32 PixelCount = SourceDimensions.x * SourceDimensions.y;
	if (PixelCount <= 0 || Source.size() < static_cast<size_t>(PixelCount * 4))
	{
		return 0;
	}

	TArray<uint8> Pixels;
	Pixels.SetNumUninitialized(PixelCount * 4);
	for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
	{
		const int32 ByteIndex = PixelIndex * 4;
		Pixels[ByteIndex] = Source[ByteIndex + 2];
		Pixels[ByteIndex + 1] = Source[ByteIndex + 1];
		Pixels[ByteIndex + 2] = Source[ByteIndex];
		Pixels[ByteIndex + 3] = Source[ByteIndex + 3];
	}

	return CreateTexture(Pixels, SourceDimensions.x, SourceDimensions.y);
}

void FH5UI_RenderInterface::ReleaseTexture(Rml::TextureHandle Texture)
{
	delete reinterpret_cast<FTextureData*>(Texture);
}

void FH5UI_RenderInterface::EnableScissorRegion(bool bEnable)
{
	bScissorEnabled = bEnable;
	RefreshSlateClips();
}

void FH5UI_RenderInterface::SetScissorRegion(Rml::Rectanglei Region)
{
	ScissorRegion = Region;
	RefreshSlateClips();
}

void FH5UI_RenderInterface::EnableClipMask(bool bEnable)
{
	bClipMaskEnabled = bEnable;
	if (!bEnable)
	{
		ClipMaskRect.Reset();
	}
	RefreshSlateClips();
}

void FH5UI_RenderInterface::RenderToClipMask(
	Rml::ClipMaskOperation Operation,
	Rml::CompiledGeometryHandle GeometryHandle,
	Rml::Vector2f Translation)
{
	const FCompiledGeometry* Geometry = reinterpret_cast<FCompiledGeometry*>(GeometryHandle);
	if (!Geometry || Geometry->Vertices.IsEmpty())
	{
		return;
	}

	// Slate's clipping stack is rectangular. RmlUi supplies transformed clip-mask
	// geometry whenever an overflow ancestor has a CSS transform (including the
	// height-responsive scale used by full-screen HUDs). Convert that geometry to
	// its transformed axis-aligned bounds so overflow remains clipped.
	const float MaximumFloat = TNumericLimits<float>::Max();
	FVector2f Minimum(MaximumFloat, MaximumFloat);
	FVector2f Maximum(-MaximumFloat, -MaximumFloat);
	for (const Rml::Vertex& Vertex : Geometry->Vertices)
	{
		Rml::Vector4f Position(
			Vertex.position.x + Translation.x,
			Vertex.position.y + Translation.y,
			0.0f,
			1.0f);
		if (CurrentTransform.IsSet())
		{
			Position = CurrentTransform.GetValue() * Position;
		}
		const float W = FMath::IsNearlyZero(Position.w) ? 1.0f : Position.w;
		const FVector2f Point(Position.x / W, Position.y / W);
		Minimum.X = FMath::Min(Minimum.X, Point.X);
		Minimum.Y = FMath::Min(Minimum.Y, Point.Y);
		Maximum.X = FMath::Max(Maximum.X, Point.X);
		Maximum.Y = FMath::Max(Maximum.Y, Point.Y);
	}

	FSlateRect NewRect(
		Minimum.X * CoordinateScale,
		Minimum.Y * CoordinateScale,
		Maximum.X * CoordinateScale,
		Maximum.Y * CoordinateScale);
	if (Operation == Rml::ClipMaskOperation::Set)
	{
		ClipMaskRect = NewRect;
	}
	else if (Operation == Rml::ClipMaskOperation::Intersect && ClipMaskRect.IsSet())
	{
		const FSlateRect Existing = ClipMaskRect.GetValue();
		ClipMaskRect = FSlateRect(
			FMath::Max(Existing.Left, NewRect.Left),
			FMath::Max(Existing.Top, NewRect.Top),
			FMath::Min(Existing.Right, NewRect.Right),
			FMath::Min(Existing.Bottom, NewRect.Bottom));
	}
	else if (Operation == Rml::ClipMaskOperation::Intersect)
	{
		ClipMaskRect = NewRect;
	}
	else
	{
		// An inverse mask cannot be represented by one Slate rectangle. Do not
		// accidentally hide the entire view; this remains an advanced unsupported
		// clip shape on the native path.
		ClipMaskRect.Reset();
	}
	RefreshSlateClips();
}

void FH5UI_FileInterface::BeginDocumentStyleCapture()
{
	CapturedGeneratedPseudoSelectors.Reset();
	bCaptureGeneratedPseudoSelectors = true;
}

TArray<FH5UI_GeneratedPseudoSelector> FH5UI_FileInterface::ConsumeGeneratedPseudoSelectors()
{
	bCaptureGeneratedPseudoSelectors = false;
	return MoveTemp(CapturedGeneratedPseudoSelectors);
}

void FH5UI_RenderInterface::SetTransform(const Rml::Matrix4f* Transform)
{
	if (Transform)
	{
		CurrentTransform = *Transform;
	}
	else
	{
		CurrentTransform.Reset();
	}
}

Rml::CompiledShaderHandle FH5UI_RenderInterface::CompileShader(const Rml::String& Name, const Rml::Dictionary& Parameters)
{
	if (!H5UIPlugin::FromUtf8(Name).Equals(TEXT("linear-gradient"), ESearchCase::IgnoreCase))
	{
		return 0;
	}

	const Rml::Variant* StartValue = Rml::GetIf(Parameters, "p0");
	const Rml::Variant* EndValue = Rml::GetIf(Parameters, "p1");
	const Rml::Variant* StopsValue = Rml::GetIf(Parameters, "color_stop_list");
	if (!StartValue || !EndValue || !StopsValue || StopsValue->GetType() != Rml::Variant::COLORSTOPLIST)
	{
		return 0;
	}

	TUniquePtr<FCompiledShader> Shader = MakeUnique<FCompiledShader>();
	if (!StartValue->GetInto(Shader->GradientStart) || !EndValue->GetInto(Shader->GradientEnd))
	{
		return 0;
	}
	Shader->ColorStops = StopsValue->GetReference<Rml::ColorStopList>();
	if (Shader->ColorStops.empty())
	{
		return 0;
	}

	if (const Rml::Variant* RepeatingValue = Rml::GetIf(Parameters, "repeating"))
	{
		RepeatingValue->GetInto(Shader->bRepeating);
	}
	return reinterpret_cast<Rml::CompiledShaderHandle>(Shader.Release());
}

void FH5UI_RenderInterface::RenderShader(
	Rml::CompiledShaderHandle ShaderHandle,
	Rml::CompiledGeometryHandle GeometryHandle,
	Rml::Vector2f Translation,
	Rml::TextureHandle TextureHandle)
{
	if (!ElementList || !PaintGeometry || !ShaderHandle || !GeometryHandle)
	{
		return;
	}

	const FCompiledShader* Shader = reinterpret_cast<FCompiledShader*>(ShaderHandle);
	const FCompiledGeometry* Geometry = reinterpret_cast<FCompiledGeometry*>(GeometryHandle);
	if (Shader->ColorStops.empty())
	{
		return;
	}

	const Rml::Vector2f GradientVector = Shader->GradientEnd - Shader->GradientStart;
	const float GradientLengthSquared = GradientVector.x * GradientVector.x + GradientVector.y * GradientVector.y;
	const FSlateRenderTransform& SlateTransform = PaintGeometry->GetAccumulatedRenderTransform();
	const TOptional<FSlateRect> ActiveClipRect = GetActiveLocalClipRect();
	auto SampleGradient = [Shader](float T)
	{
		if (Shader->bRepeating)
		{
			T = T - FMath::FloorToFloat(T);
		}
		else
		{
			T = FMath::Clamp(T, 0.0f, 1.0f);
		}

		const Rml::ColorStop* Previous = &Shader->ColorStops.front();
		for (const Rml::ColorStop& Next : Shader->ColorStops)
		{
			if (T <= Next.position.number)
			{
				const float Span = Next.position.number - Previous->position.number;
				const float LocalT = Span > KINDA_SMALL_NUMBER
					? FMath::Clamp((T - Previous->position.number) / Span, 0.0f, 1.0f)
					: 1.0f;
				return FColor(
					FMath::RoundToInt(FMath::Lerp(float(Previous->color.red), float(Next.color.red), LocalT)),
					FMath::RoundToInt(FMath::Lerp(float(Previous->color.green), float(Next.color.green), LocalT)),
					FMath::RoundToInt(FMath::Lerp(float(Previous->color.blue), float(Next.color.blue), LocalT)),
					FMath::RoundToInt(FMath::Lerp(float(Previous->color.alpha), float(Next.color.alpha), LocalT)));
			}
			Previous = &Next;
		}

		return FColor(Previous->color.red, Previous->color.green, Previous->color.blue, Previous->color.alpha);
	};

	const FSlateResourceHandle ResourceHandle = ResolveResourceHandle(reinterpret_cast<FTextureData*>(TextureHandle));
	FScopedSlateVertices VertexBuffer(*this, Geometry->Vertices.Num());
	TArray<FSlateVertex>& SlateVertices = VertexBuffer.Vertices;
	for (const Rml::Vertex& Vertex : Geometry->Vertices)
	{
		Rml::Vector4f Position(Vertex.position.x + Translation.x, Vertex.position.y + Translation.y, 0.0f, 1.0f);
		if (CurrentTransform.IsSet())
		{
			Position = CurrentTransform.GetValue() * Position;
		}
		const float W = FMath::IsNearlyZero(Position.w) ? 1.0f : Position.w;
		FVector2f LocalPosition(Position.x / W, Position.y / W);
		LocalPosition *= CoordinateScale;
		if (ActiveClipRect.IsSet())
		{
			const FSlateRect Clip = ActiveClipRect.GetValue();
			const FVector2f Unclipped = LocalPosition;
			LocalPosition.X = FMath::Clamp(Unclipped.X, Clip.Left, Clip.Right);
			LocalPosition.Y = FMath::Clamp(Unclipped.Y, Clip.Top, Clip.Bottom);
			if (!LocalPosition.Equals(Unclipped, KINDA_SMALL_NUMBER))
			{
				++CurrentStats.ClampedVertices;
			}
		}
		const Rml::Vector2f TexCoord = Vertex.tex_coord;
		const float T = GradientLengthSquared > KINDA_SMALL_NUMBER
			? ((TexCoord.x - Shader->GradientStart.x) * GradientVector.x + (TexCoord.y - Shader->GradientStart.y) * GradientVector.y) / GradientLengthSquared
			: 0.0f;
		SlateVertices.Add(FSlateVertex::Make(
			SlateTransform,
			LocalPosition,
			FVector2f(Vertex.tex_coord.x, Vertex.tex_coord.y),
			SampleGradient(T)));
	}

	FSlateDrawElement::MakeCustomVerts(
		*ElementList,
		LayerId++,
		ResourceHandle,
		SlateVertices,
		Geometry->Indices,
		nullptr,
		0,
		0,
		ESlateDrawEffect::PreMultipliedAlpha);
	++CurrentStats.DrawBatches;
	CurrentStats.Vertices += SlateVertices.Num();
	CurrentStats.Triangles += Geometry->Indices.Num() / 3;
}

void FH5UI_RenderInterface::ReleaseShader(Rml::CompiledShaderHandle Shader)
{
	delete reinterpret_cast<FCompiledShader*>(Shader);
}

Rml::TextureHandle FH5UI_RenderInterface::CreateTexture(
	const TArray<uint8>& PremultipliedBGRA,
	int32 Width,
	int32 Height)
{
	if (!FSlateApplication::IsInitialized() || Width <= 0 || Height <= 0)
	{
		return 0;
	}

	const FName ResourceName(*FString::Printf(TEXT("H5UIPlugin.Texture.%llu"), ++TextureSerial));
	if (!FSlateApplication::Get().GetRenderer()->GenerateDynamicImageResource(
		ResourceName,
		static_cast<uint32>(Width),
		static_cast<uint32>(Height),
		PremultipliedBGRA))
	{
		return 0;
	}

	TUniquePtr<FTextureData> Texture = MakeUnique<FTextureData>();
	Texture->Size = FIntPoint(Width, Height);
	Texture->Brush = MakeShared<FSlateDynamicImageBrush>(ResourceName, FVector2D(Width, Height));
	return reinterpret_cast<Rml::TextureHandle>(Texture.Release());
}

TOptional<FSlateRect> FH5UI_RenderInterface::GetActiveLocalClipRect() const
{
	TOptional<FSlateRect> Result;
	if (bScissorEnabled && ScissorRegion.Valid())
	{
		Result = FSlateRect(
			static_cast<float>(ScissorRegion.p0.x) * CoordinateScale,
			static_cast<float>(ScissorRegion.p0.y) * CoordinateScale,
			static_cast<float>(ScissorRegion.p1.x) * CoordinateScale,
			static_cast<float>(ScissorRegion.p1.y) * CoordinateScale);
	}

	if (bClipMaskEnabled && ClipMaskRect.IsSet())
	{
		const FSlateRect Mask = ClipMaskRect.GetValue();
		if (Result.IsSet())
		{
			const FSlateRect Existing = Result.GetValue();
			Result = FSlateRect(
				FMath::Max(Existing.Left, Mask.Left),
				FMath::Max(Existing.Top, Mask.Top),
				FMath::Min(Existing.Right, Mask.Right),
				FMath::Min(Existing.Bottom, Mask.Bottom));
		}
		else
		{
			Result = Mask;
		}
	}

	if (Result.IsSet())
	{
		const FSlateRect Rect = Result.GetValue();
		if (Rect.Right <= Rect.Left || Rect.Bottom <= Rect.Top)
		{
			return FSlateRect(Rect.Left, Rect.Top, Rect.Left, Rect.Top);
		}
	}
	return Result;
}

void FH5UI_RenderInterface::RefreshSlateClips()
{
	if (!ElementList || !PaintGeometry)
	{
		return;
	}

	if (bSlateMaskClipPushed)
	{
		ElementList->PopClip();
		bSlateMaskClipPushed = false;
	}
	if (bSlateScissorClipPushed)
	{
		ElementList->PopClip();
		bSlateScissorClipPushed = false;
	}

	if (bScissorEnabled && ScissorRegion.Valid())
	{
		const FVector2D TopLeft = PaintGeometry->LocalToAbsolute(
			FVector2D(ScissorRegion.p0.x, ScissorRegion.p0.y) * CoordinateScale);
		const FVector2D BottomRight = PaintGeometry->LocalToAbsolute(
			FVector2D(ScissorRegion.p1.x, ScissorRegion.p1.y) * CoordinateScale);
		ElementList->PushClip(FSlateClippingZone(FSlateRect(
			TopLeft.X,
			TopLeft.Y,
			BottomRight.X,
			BottomRight.Y)));
		bSlateScissorClipPushed = true;
	}

	if (bClipMaskEnabled && ClipMaskRect.IsSet())
	{
		const FSlateRect LocalRect = ClipMaskRect.GetValue();
		const FVector2D TopLeft = PaintGeometry->LocalToAbsolute(
			FVector2D(LocalRect.Left, LocalRect.Top));
		const FVector2D BottomRight = PaintGeometry->LocalToAbsolute(
			FVector2D(LocalRect.Right, LocalRect.Bottom));
		if (BottomRight.X > TopLeft.X && BottomRight.Y > TopLeft.Y)
		{
			ElementList->PushClip(FSlateClippingZone(FSlateRect(
				TopLeft.X,
				TopLeft.Y,
				BottomRight.X,
				BottomRight.Y)));
			bSlateMaskClipPushed = true;
		}
	}
}

FSlateResourceHandle FH5UI_RenderInterface::ResolveResourceHandle(FTextureData* TextureData) const
{
	const FSlateBrush* Brush = TextureData && TextureData->Brush.IsValid()
		? static_cast<const FSlateBrush*>(TextureData->Brush.Get())
		: FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	return FSlateApplication::Get().GetRenderer()->GetResourceHandle(*Brush);
}
