import { useEffect, useRef, useState } from "react";

type MirrorState = "idle" | "loading" | "active" | "denied" | "error";

export function Mirror() {
  const videoRef = useRef<HTMLVideoElement>(null);
  const [state, setState] = useState<MirrorState>("idle");
  const streamRef = useRef<MediaStream | null>(null);

  useEffect(() => {
    setState("loading");

    navigator.mediaDevices
      .getUserMedia({ video: { facingMode: "user" }, audio: false })
      .then((stream) => {
        streamRef.current = stream;
        if (videoRef.current) {
          videoRef.current.srcObject = stream;
        }
        setState("active");
      })
      .catch((err: DOMException) => {
        if (err.name === "NotAllowedError" || err.name === "PermissionDeniedError") {
          setState("denied");
        } else {
          setState("error");
        }
      });

    return () => {
      streamRef.current?.getTracks().forEach((t) => t.stop());
    };
  }, []);

  return (
    <div className="mirror-scene">
      <div className="mirror-frame">
        <div className="mirror-surface">
          {state === "active" && (
            <video
              ref={videoRef}
              autoPlay
              playsInline
              muted
              className="mirror-video"
            />
          )}
          {state !== "active" && (
            <div className="mirror-overlay">
              {state === "loading" && (
                <p className="mirror-message">Requesting camera access…</p>
              )}
              {state === "denied" && (
                <p className="mirror-message">
                  Camera access was denied.
                  <br />
                  <span className="mirror-hint">
                    Allow camera permission and reload the page.
                  </span>
                </p>
              )}
              {state === "error" && (
                <p className="mirror-message">
                  Could not access camera.
                  <br />
                  <span className="mirror-hint">
                    Make sure a camera is connected and try again.
                  </span>
                </p>
              )}
              {state === "idle" && null}
            </div>
          )}
          <div className="mirror-glare" />
        </div>
        <div className="mirror-label">MIRROR</div>
      </div>
    </div>
  );
}
