# chouchouLink - Ableton Live Control Surface for chouchouPlugplugin2.
#
# Answers "selected_clip" on 127.0.0.1:9017 with the file behind the selected Arrangement
# audio clip and where that file's first sample sits on the song timeline, so chouchou can
# hand the whole file to SpectraLayers and keep it aligned.
#
# Install: copy this folder to ~/Music/Ableton/User Library/Remote Scripts/chouchouLink,
# restart Live, then pick "chouchouLink" as a Control Surface (no MIDI ports needed).

import json
import queue
import socket
import threading

from _Framework.ControlSurface import ControlSurface

PORT = 9017


def create_instance(c_instance):
    return ChouChouLink(c_instance)


class ChouChouLink(ControlSurface):
    def __init__(self, c_instance):
        ControlSurface.__init__(self, c_instance)
        self._requests = queue.Queue()
        self._running = True
        self._server = None

        try:
            self._server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._server.bind(("127.0.0.1", PORT))
            self._server.listen(4)
            self._server.settimeout(0.5)
        except Exception as e:
            self.log_message("chouchouLink: cannot listen on %d: %s" % (PORT, e))
            self._server = None
            return

        thread = threading.Thread(target=self._serve)
        thread.daemon = True
        thread.start()
        self.log_message("chouchouLink listening on 127.0.0.1:%d" % PORT)

    def disconnect(self):
        self._running = False
        if self._server is not None:
            try:
                self._server.close()
            except Exception:
                pass
        ControlSurface.disconnect(self)

    # Socket side (background threads). The Live API is only touched in update_display.
    def _serve(self):
        while self._running:
            try:
                conn, _ = self._server.accept()
            except socket.timeout:
                continue
            except Exception:
                break

            worker = threading.Thread(target=self._handle, args=(conn,))
            worker.daemon = True
            worker.start()

    def _handle(self, conn):
        try:
            conn.settimeout(3.0)
            data = b""
            while b"\n" not in data:
                chunk = conn.recv(1024)
                if not chunk:
                    break
                data += chunk

            reply = queue.Queue()
            self._requests.put((data.decode("utf-8", "replace").strip(), reply))
            try:
                result = reply.get(timeout=3.0)
            except queue.Empty:
                result = {"ok": False, "error": "Live did not answer in time"}

            conn.sendall((json.dumps(result) + "\n").encode("utf-8"))
        except Exception as e:
            self.log_message("chouchouLink: request failed: %s" % e)
        finally:
            try:
                conn.close()
            except Exception:
                pass

    # Live main thread (called about every 100 ms).
    def update_display(self):
        ControlSurface.update_display(self)
        while True:
            try:
                command, reply = self._requests.get_nowait()
            except queue.Empty:
                break

            try:
                reply.put(self._answer(command))
            except Exception as e:
                reply.put({"ok": False, "error": str(e)})

    def _answer(self, command):
        if command != "selected_clip":
            return {"ok": False, "error": "unknown command: %s" % command}

        song = self.song()
        track = song.view.selected_track
        clip = None

        picked = "detail"
        detail = song.view.detail_clip
        if detail is not None and detail.is_audio_clip and getattr(detail, "is_arrangement_clip", False):
            clip = detail

        if clip is None:
            clips = [c for c in getattr(track, "arrangement_clips", []) if c.is_audio_clip]
            if not clips:
                return {"ok": False,
                        "error": "no audio clip in the Arrangement on track '%s' - select one first" % track.name}
            now = song.current_song_time
            under = [c for c in clips if c.start_time <= now < c.end_time]
            clip, picked = (under[0], "playhead") if under else (clips[0], "first")

        if not clip.file_path:
            return {"ok": False, "error": "clip '%s' has no file on disk" % clip.name}

        tempo = song.tempo
        clip_start_seconds = clip.start_time * 60.0 / tempo

        # Unwarped clips keep markers in seconds; warped clips in beats.
        if clip.warping:
            offset_seconds = self._beat_to_file_seconds(clip, clip.start_marker, tempo)
        else:
            offset_seconds = clip.start_marker

        return {
            "ok": True,
            "file_path": clip.file_path,
            "file_start_seconds": clip_start_seconds - offset_seconds,
            "clip_name": clip.name,
            "track_name": track.name,
            "warping": bool(clip.warping),
            "tempo": tempo,
            "picked": picked,
            "start_beat": clip.start_time,
            "end_beat": clip.end_time,
            "tempo_automated": self._tempo_automated(song),
        }

    @staticmethod
    def _tempo_automated(song):
        # Live's API cannot read the Arrangement tempo envelope, only whether one exists.
        try:
            return song.master_track.mixer_device.song_tempo.automation_state != 0
        except Exception:
            return False

    @staticmethod
    def _beat_to_file_seconds(clip, beat, tempo):
        markers = list(getattr(clip, "warp_markers", None) or [])
        points = sorted((m.beat_time, m.sample_time) for m in markers)
        if len(points) >= 2:
            for (b0, s0), (b1, s1) in zip(points, points[1:]):
                if beat <= b1 or (b1, s1) == points[-1]:
                    if b1 == b0:
                        return s0
                    return s0 + (beat - b0) * (s1 - s0) / (b1 - b0)
        return beat * 60.0 / tempo
