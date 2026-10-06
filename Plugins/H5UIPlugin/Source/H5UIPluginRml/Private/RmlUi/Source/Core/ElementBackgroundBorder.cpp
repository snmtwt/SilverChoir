#include "ElementBackgroundBorder.h"
#include "../../Include/RmlUi/Core/Box.h"
#include "../../Include/RmlUi/Core/ComputedValues.h"
#include "../../Include/RmlUi/Core/Context.h"
#include "../../Include/RmlUi/Core/DecorationTypes.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/MeshUtilities.h"
#include "../../Include/RmlUi/Core/Profiling.h"
#include "../../Include/RmlUi/Core/RenderManager.h"
#include "BoxShadowCache.h"
#include "GeometryBoxShadow.h"

namespace Rml {

namespace {

ColourbPremultiplied ScalePremultipliedColour(ColourbPremultiplied colour, float scale)
{
	scale = Math::Clamp(scale, 0.f, 1.f);
	colour.red = byte(float(colour.red) * scale);
	colour.green = byte(float(colour.green) * scale);
	colour.blue = byte(float(colour.blue) * scale);
	colour.alpha = byte(float(colour.alpha) * scale);
	return colour;
}

float SignedDistanceToRoundedBox(Vector2f position, Vector2f box_position, Vector2f box_size, const CornerSizes& corner_radii)
{
	const Vector2f half_size = 0.5f * box_size;
	const Vector2f local_position = position - box_position - half_size;
	const bool is_right = local_position.x >= 0.f;
	const bool is_bottom = local_position.y >= 0.f;
	const float corner_radius = corner_radii[is_bottom ? (is_right ? 2 : 3) : (is_right ? 1 : 0)];
	const Vector2f inner_half_size = Math::Max(half_size - Vector2f(corner_radius), Vector2f(0.f));
	const Vector2f q = Vector2f(Math::Absolute(local_position.x), Math::Absolute(local_position.y)) - inner_half_size;
	const Vector2f outside = Math::Max(q, Vector2f(0.f));
	return Math::SquareRoot(outside.x * outside.x + outside.y * outside.y) + Math::Min(Math::Max(q.x, q.y), 0.f) - corner_radius;
}

// Slate's geometry renderer has no offscreen blur target. Sample a distance-field
// approximation instead of using one large alpha ring: the extra vertices remove the
// diagonal interpolation seams that were visible on wide, translucent panels.
void GenerateSoftShadowMesh(
	Mesh& mesh,
	Vector2f box_position,
	Vector2f box_size,
	const CornerSizes& corner_radii,
	float blur_extension,
	ColourbPremultiplied shadow_colour)
{
	if (box_size.x <= 0.f || box_size.y <= 0.f)
		return;

	const float safe_blur_extension = Math::Max(blur_extension, 1.f);
	const Vector2f shadow_position = box_position - Vector2f(safe_blur_extension);
	const Vector2f shadow_size = box_size + Vector2f(2.f * safe_blur_extension);
	const float sample_spacing = Math::Clamp(safe_blur_extension / 5.f, 1.5f, 4.f);
	const int columns = Math::Clamp(Math::RoundUpToInteger(shadow_size.x / sample_spacing), 1, 96);
	const int rows = Math::Clamp(Math::RoundUpToInteger(shadow_size.y / sample_spacing), 1, 96);
	const int vertex_start = (int)mesh.vertices.size();
	const int vertex_count = (columns + 1) * (rows + 1);
	const int index_start = (int)mesh.indices.size();
	mesh.vertices.resize(mesh.vertices.size() + vertex_count);
	mesh.indices.resize(mesh.indices.size() + columns * rows * 6);

	for (int row = 0; row <= rows; ++row)
	{
		const float y = shadow_position.y + shadow_size.y * (float(row) / float(rows));
		for (int column = 0; column <= columns; ++column)
		{
			const float x = shadow_position.x + shadow_size.x * (float(column) / float(columns));
			const float distance = Math::Max(0.f, SignedDistanceToRoundedBox(Vector2f(x, y), box_position, box_size, corner_radii));
			const float normalized_distance = Math::Clamp(distance / safe_blur_extension, 0.f, 1.f);
			const float gaussian_alpha = Math::Exp(-5.f * normalized_distance * normalized_distance);
			Rml::Vertex& vertex = mesh.vertices[vertex_start + row * (columns + 1) + column];
			vertex.position = Vector2f(x, y);
			vertex.colour = ScalePremultipliedColour(shadow_colour, gaussian_alpha);
			vertex.tex_coord = Vector2f(0.f);
		}
	}

	int write_index = index_start;
	for (int row = 0; row < rows; ++row)
	{
		for (int column = 0; column < columns; ++column)
		{
			const int top_left = vertex_start + row * (columns + 1) + column;
			const int top_right = top_left + 1;
			const int bottom_left = top_left + columns + 1;
			const int bottom_right = bottom_left + 1;
			mesh.indices[write_index++] = top_left;
			mesh.indices[write_index++] = bottom_left;
			mesh.indices[write_index++] = top_right;
			mesh.indices[write_index++] = top_right;
			mesh.indices[write_index++] = bottom_left;
			mesh.indices[write_index++] = bottom_right;
		}
	}
}

void GenerateBasicBoxShadows(Mesh& mesh, Element* element, float element_opacity)
{
	const Property* property = element->GetLocalProperty(PropertyId::BoxShadow);
	if (!property || property->value.GetType() != Variant::BOXSHADOWLIST)
		return;

	const BoxShadowList shadows = property->value.Get<BoxShadowList>();
	for (auto shadow_it = shadows.rbegin(); shadow_it != shadows.rend(); ++shadow_it)
	{
		const BoxShadow& shadow = *shadow_it;
		if (shadow.inset)
			continue;

		const float offset_x = element->ResolveLength(shadow.offset_x);
		const float offset_y = element->ResolveLength(shadow.offset_y);
		const float blur = Math::Max(0.f, element->ResolveLength(shadow.blur_radius));
		const float spread = element->ResolveLength(shadow.spread_distance);
		for (int box_index = 0; box_index < element->GetNumBoxes(); ++box_index)
		{
			RenderBox shadow_box = element->GetRenderBox(BoxArea::Border, box_index);
			const float safe_spread = Math::Max(-0.49f * Math::Min(shadow_box.GetFillSize().x, shadow_box.GetFillSize().y), spread);
			shadow_box.SetBorderOffset(
				shadow_box.GetBorderOffset() + Vector2f(offset_x - safe_spread, offset_y - safe_spread));
			shadow_box.SetFillSize(Math::Max(
				shadow_box.GetFillSize() + Vector2f(2.f * safe_spread),
				Vector2f(0.001f)));
			shadow_box.SetBorderWidths({0.f, 0.f, 0.f, 0.f});

			CornerSizes radii = shadow_box.GetBorderRadius();
			for (float& radius : radii)
			{
				radius = Math::Max(0.f, radius + safe_spread);
			}
			shadow_box.SetBorderRadius(radii);

			GenerateSoftShadowMesh(
				mesh,
				shadow_box.GetBorderOffset(),
				shadow_box.GetFillSize(),
				shadow_box.GetBorderRadius(),
				1.5f * blur,
				ScalePremultipliedColour(shadow.color, element_opacity));
		}
	}
}

} // namespace

ElementBackgroundBorder::ElementBackgroundBorder() {}

void ElementBackgroundBorder::Render(Element* element)
{
	if (background_dirty || border_dirty)
	{
		for (auto& background : backgrounds)
		{
			if (background.first != BackgroundType::BackgroundBorder)
				background.second.geometry.Release();
		}

		GenerateGeometry(element);

		background_dirty = false;
		border_dirty = false;
	}

	if (Background* shadow = GetBackground(BackgroundType::BoxShadowAndBackgroundBorder))
	{
		const Vector2f offset = element->GetAbsoluteOffset(BoxArea::Border);
		shadow->box_shadow_and_background_border->geometry.Render(offset, shadow->box_shadow_and_background_border->texture);
	}
	else if (Background* background = GetBackground(BackgroundType::BackgroundBorder))
	{
		const Vector2f offset = element->GetAbsoluteOffset(BoxArea::Border);
		background->geometry.Render(offset);
	}
}

void ElementBackgroundBorder::DirtyBackground()
{
	background_dirty = true;
}

void ElementBackgroundBorder::DirtyBorder()
{
	border_dirty = true;
}

Geometry* ElementBackgroundBorder::GetClipGeometry(Element* element, BoxArea clip_area)
{
	BackgroundType type = {};
	switch (clip_area)
	{
	case Rml::BoxArea::Border: type = BackgroundType::ClipBorder; break;
	case Rml::BoxArea::Padding: type = BackgroundType::ClipPadding; break;
	case Rml::BoxArea::Content: type = BackgroundType::ClipContent; break;
	default: RMLUI_ERROR; return nullptr;
	}

	RenderManager* render_manager = element->GetRenderManager();
	Geometry& geometry = GetOrCreateBackground(type).geometry;
	if (render_manager && !geometry)
	{
		Mesh mesh = geometry.Release(Geometry::ReleaseMode::ClearMesh);
		MeshUtilities::GenerateBackground(mesh, element->GetRenderBox(clip_area), ColourbPremultiplied(255));
		geometry = render_manager->MakeGeometry(std::move(mesh));
	}

	return &geometry;
}

ElementBackgroundBorder::Background* ElementBackgroundBorder::GetBackground(BackgroundType type)
{
	auto it = backgrounds.find(type);
	if (it != backgrounds.end())
		return &it->second;
	return nullptr;
}

ElementBackgroundBorder::Background& ElementBackgroundBorder::GetOrCreateBackground(BackgroundType type)
{
	auto it = backgrounds.find(type);
	if (it != backgrounds.end())
		return it->second;

	Background& background = backgrounds[type];
	return background;
}

void ElementBackgroundBorder::EraseBackground(BackgroundType type)
{
	backgrounds.erase(type);
}

void ElementBackgroundBorder::GenerateGeometry(Element* element)
{
	RMLUI_ZoneScoped;
	RenderManager* render_manager = element->GetRenderManager();
	if (!render_manager)
		return;

	const ComputedValues& computed = element->GetComputedValues();
	// Box shadows are normally baked through an offscreen layer. On a basic geometry
	// renderer, attempting that process would draw the temporary shadow mesh directly
	// into the base framebuffer and later render its missing texture as a white quad.
	// Keep the element's regular background and border as a predictable CSS fallback.
	const bool has_box_shadow = computed.has_box_shadow() && render_manager->SupportsLayerRendering();

	if (has_box_shadow)
	{
		// The box shadow geometry also includes the element's background and border, thus we can skip the normal background generation.
		EraseBackground(BackgroundType::BackgroundBorder);
		Background& shadow_background = GetOrCreateBackground(BackgroundType::BoxShadowAndBackgroundBorder);
		shadow_background.box_shadow_and_background_border = BoxShadowCache::GetHandle(element, computed);
		return;
	}

	EraseBackground(BackgroundType::BoxShadowAndBackgroundBorder);

	const float opacity = computed.opacity();
	ColourbPremultiplied background_color = computed.background_color().ToPremultiplied(opacity);
	Array<ColourbPremultiplied, 4> border_colors = {
		computed.border_top_color().ToPremultiplied(opacity),
		computed.border_right_color().ToPremultiplied(opacity),
		computed.border_bottom_color().ToPremultiplied(opacity),
		computed.border_left_color().ToPremultiplied(opacity),
	};

	Geometry& geometry = GetOrCreateBackground(BackgroundType::BackgroundBorder).geometry;
	Mesh mesh = geometry.Release(Geometry::ReleaseMode::ClearMesh);
	if (computed.has_box_shadow() && !render_manager->SupportsLayerRendering())
		GenerateBasicBoxShadows(mesh, element, opacity);

	for (int i = 0; i < element->GetNumBoxes(); i++)
		MeshUtilities::GenerateBackgroundBorder(mesh, element->GetRenderBox(BoxArea::Padding, i), background_color, border_colors.data());

	geometry = render_manager->MakeGeometry(std::move(mesh));
}

} // namespace Rml
