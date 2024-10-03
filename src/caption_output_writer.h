#ifndef OBS_GOOGLE_CAPTION_PLUGIN_CAPTION_OUTPUT_WRITER_H
#define OBS_GOOGLE_CAPTION_PLUGIN_CAPTION_OUTPUT_WRITER_H

#include "thirdparty/cameron314/blockingconcurrentqueue.h"
#include "log.c"
#include "SourceCaptioner.h"
#include <locale>
#include <codecvt> // Pour gérer l'encodage UTF-8

static void caption_output_writer_loop(shared_ptr<CaptionOutputControl<int>> control, bool to_stream) {
    // TODO: minimum_time_between_captions arg to optionally hold next caption if still too soon after previous one
    // just skip in between ones, maybe option to fall behind instead as well?

    string to_what(to_stream ? "streaming" : "recording");
    info_log("caption_output_writer_loop %s starting", to_what.c_str());

    string previous_line;
    CaptionOutput caption_output;
    int active_delay_sec;
    bool got_item;
    obs_output_t *output = nullptr;

    double waited_left_secs = 0;
    while (!control->stop) {
        control->caption_queue.wait_dequeue(caption_output);

        if (control->stop)
            break;

        if (!caption_output.output_result) {
            info_log("got empty CaptionOutput.output_result???");
            continue;
        }

        if (output) {
            obs_output_release(output);
            output = nullptr;
        }

        if (to_stream)
            output = obs_frontend_get_streaming_output();
        else
            output = obs_frontend_get_recording_output();

        if (!output) {
            info_log("built caption lines, no output 1, not sending, not %s?: '%s'", to_what.c_str(),
                     caption_output.output_result->output_line.c_str());
            continue;
        }

        if (!caption_output.is_clearance && caption_output.output_result->output_line.empty()) {
            debug_log("ingoring empty non clearance, %s", to_what.c_str());
            continue;
        }

        if (caption_output.output_result->output_line == previous_line) {
            // Ignorer les doublons
            continue;
        }
        previous_line = caption_output.output_result->output_line;

        waited_left_secs = 0;

        active_delay_sec = obs_output_get_active_delay(output);
        if (active_delay_sec) {
            auto since_creation = chrono::steady_clock::now() - caption_output.output_result->caption_result.received_at;
            chrono::seconds wanted_delay(active_delay_sec);

            auto wait_left = wanted_delay - since_creation;
            if (wait_left > wanted_delay) {
                info_log("capping delay, wtf, negative duration?, got %f, max %f",
                         chrono::duration_cast<std::chrono::duration<double >>(wait_left).count(),
                         chrono::duration_cast<std::chrono::duration<double >>(wanted_delay).count());
                wait_left = wanted_delay;
            }

            waited_left_secs = chrono::duration_cast<std::chrono::duration<double >>(wait_left).count();
            debug_log("caption_output_writer_loop %s sleeping for %f seconds",
                      to_what.c_str(), waited_left_secs);

            obs_output_release(output);
            output = nullptr;

            std::this_thread::sleep_for(wait_left);

            if (control->stop)
                break;

            if (to_stream)
                output = obs_frontend_get_streaming_output();
            else
                output = obs_frontend_get_recording_output();

            if (!output) {
                info_log("built caption lines, no output 2, not sending, not %s?: '%s'", to_what.c_str(),
                         caption_output.output_result->output_line.c_str());
                continue;
            }
        }

        // Correction : Gestion de l'encodage UTF-8 pour éviter les symboles incorrects
        std::string caption_output_line = caption_output.output_result->output_line;

        try {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            std::wstring wide_line = converter.from_bytes(caption_output_line); // Conversion UTF-8 vers wide_string
            caption_output_line = converter.to_bytes(wide_line); // Reconvertir en UTF-8 après vérification
        } catch (const std::range_error&) {
            info_log("Erreur : encodage invalide détecté dans la ligne : '%s'", caption_output_line.c_str());
            continue; // Ignorer cette ligne en cas d'erreur d'encodage
        }

        debug_log("sending caption %s line now, waited %f: '%s'",
                  to_what.c_str(), waited_left_secs, caption_output_line.c_str());

        // Envoyer le texte correctement encodé
        obs_output_output_caption_text2(output, caption_output_line.c_str(), 0.0);
    }

    if (output) {
        obs_output_release(output);
        output = nullptr;
    }

    info_log("caption_output_writer_loop %s done", to_what.c_str());
}

#endif // OBS_GOOGLE_CAPTION_PLUGIN_CAPTION_OUTPUT_WRITER_H

