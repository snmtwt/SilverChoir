#include "FontFace.h"
#include "../../../Include/RmlUi/Core/Log.h"
#include "../../../Include/RmlUi/Core/Math.h"
#include "FontFaceHandleDefault.h"
#include "FreeTypeInterface.h"

namespace Rml {

FontFace::FontFace(FontFaceHandleFreetype _face, Style::FontStyle _style, Style::FontWeight _weight)
{
	style = _style;
	weight = _weight;
	face = _face;
}

FontFace::~FontFace()
{
	if (face)
		FreeType::ReleaseFace(face);
}

Style::FontStyle FontFace::GetStyle() const
{
	return style;
}

Style::FontWeight FontFace::GetWeight() const
{
	return weight;
}

FontFaceHandleDefault* FontFace::GetHandle(int size, bool load_default_glyphs, float rasterization_scale)
{
	const int logical_size = Math::Max(size, 1);
	const float safe_rasterization_scale = Math::Clamp(rasterization_scale, 1.f, 4.f);
	const int raster_size = Math::Max(Math::RoundToInteger((float)logical_size * safe_rasterization_scale), 1);
	const uint32_t scale_key = (uint32_t)Math::RoundToInteger(safe_rasterization_scale * 64.f);
	const uint64_t handle_key = (uint64_t(uint32_t(logical_size)) << 32) | uint64_t(scale_key);
	auto it = handles.find(handle_key);
	if (it != handles.end())
		return it->second.get();

	// See if this face has been released.
	if (!face)
	{
		Log::Message(Log::LT_WARNING, "Font face has been released, unable to generate new handle.");
		return nullptr;
	}

	// Construct and initialise the new handle.
	auto handle = MakeUnique<FontFaceHandleDefault>();
	if (!handle->Initialize(face, logical_size, raster_size, safe_rasterization_scale, load_default_glyphs))
	{
		handles[handle_key] = nullptr;
		return nullptr;
	}

	FontFaceHandleDefault* result = handle.get();

	// Save the new handle to the font face
	handles[handle_key] = std::move(handle);

	return result;
}

void FontFace::ReleaseFontResources()
{
	HandleMap().swap(handles);
}

} // namespace Rml
