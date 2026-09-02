#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/util/duration.hpp>

#include <fstream>
#include <algorithm>

class wayfire_zoom_screen : public wf::per_output_plugin_instance_t
{
    enum class interpolation_method_t
    {
        LINEAR  = 0,
        NEAREST = 1,
    };

    wf::option_wrapper_t<wf::keybinding_t> modifier{"zoom/modifier"};
    wf::option_wrapper_t<double> speed{"zoom/speed"};
    wf::option_wrapper_t<wf::animation_description_t> smoothing_duration{"zoom/smoothing_duration"};
    wf::option_wrapper_t<int> interpolation_method{"zoom/interpolation_method"};
    wf::animation::simple_animation_t progression{smoothing_duration};
    bool hook_set = false;

    wf::plugin_activation_data_t grab_interface = {
        .name = "zoom",
        .capabilities = 0,
    };

  public:
    void init() override
    {
        progression.set(1, 1);
        output->add_axis(modifier, &axis);
    }

    void update_zoom_target(float delta)
    {
        float target = progression.end;
        target -= target * delta * speed;
        target = wf::clamp(target, 1.0f, 50.0f);

        if (target != progression.end)
        {
            progression.animate(target);

            if (!hook_set)
            {
                hook_set = true;
                output->render->add_post(&render_hook);
                output->render->set_redraw_always();
            }
        }
    }

    wf::axis_callback axis = [=] (wlr_pointer_axis_event *ev)
    {
        if (!output->can_activate_plugin(&grab_interface))
        {
            return false;
        }

        if (ev->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            return false;
        }

        update_zoom_target(ev->delta);

        return true;
    };

    wf::post_hook_t render_hook = [=] (wf::auxilliary_buffer_t& source,
                                       const wf::render_buffer_t& destination)
    {
        auto w = destination.get_size().width;
        auto h = destination.get_size().height;

        if ((w <= 0) || (h <= 0))
        {
            LOGE("Invalid output size in zoom plugin!");
            return;
        }

        auto oc = output->get_cursor_position();

        double x, y;
        wlr_box b = wf::to_integer_box(output->get_relative_geometry());
        wlr_box_closest_point(&b, oc.x, oc.y, &x, &y);

        /* get rotation & scale */
        wf::geometry_t box = {x, y, 1, 1};
        box = output->render->get_target_framebuffer().framebuffer_geometry_from_geometry_box(box);
        x = box.x;
        y = box.y;

        const float factor = (float)progression;

        /*
         * Source rectangle centered around the cursor.
         *
         * The cursor is at the center of the destination, so the
         * source point corresponding to the cursor is also at (x, y).
         */
        const float tw = w / factor;
        const float th = h / factor;

        const float x1 = x - tw / 2.0f;
        const float y1 = y - th / 2.0f;

        /*
         * wlroots requires the source rectangle to stay inside
         * the texture, so clip it.
         */
        const float sx1 = std::max(x1, 0.0f);
        const float sy1 = std::max(y1, 0.0f);
        const float sx2 = std::min(x1 + tw, (float)w);
        const float sy2 = std::min(y1 + th, (float)h);

        const float sw = sx2 - sx1;
        const float sh = sy2 - sy1;

        auto filter_mode =
            (interpolation_method == (int)interpolation_method_t::NEAREST) ?
            WLR_SCALE_FILTER_NEAREST :
            WLR_SCALE_FILTER_BILINEAR;

        std::ofstream log("/tmp/wayfire-zoom.log", std::ios::app);

        log << "x=" << x
            << " y=" << y
            << " factor=" << factor
            << " requested_src=("
            << x1 << "," << y1 << "," << tw << "," << th
            << ") clipped_src=("
            << sx1 << "," << sy1 << "," << sw << "," << sh
            << ")";

        if (sw > 0.0f && sh > 0.0f)
        {
            /*
             * Map the clipped source rectangle back to the
             * corresponding destination position.
             */
            const float dx = (sx1 - x1) * factor;
            const float dy = (sy1 - y1) * factor;
            const float dw = sw * factor;
            const float dh = sh * factor;

            log << " dst=("
                << dx << "," << dy << "," << dw << "," << dh
                << ")";

            destination.blit(
                source,
                {sx1, sy1, sw, sh},
                {dx, dy, dw, dh},
                filter_mode);
        }

        log << '\n';

        if (!progression.running() && (progression - 1 <= 0.01))
        {
            unset_hook();
        }
    };

    void unset_hook()
    {
        output->render->set_redraw_always(false);
        output->render->rem_post(&render_hook);
        hook_set = false;
    }

    void fini() override
    {
        if (hook_set)
        {
            output->render->rem_post(&render_hook);
        }

        output->rem_binding(&axis);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_zoom_screen>);
