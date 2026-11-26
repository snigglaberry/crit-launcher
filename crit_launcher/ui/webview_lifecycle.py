"""Keep WebView2 bridge calls from outliving the native window."""

import json


def cancel_edge_scripts_on_close(window):
    if window.gui.renderer != "edgechromium":
        return

    from System import Func, Object
    from System.Threading import CancellationTokenSource

    form = window.native
    browser = form.browser
    view = browser.webview
    cancellation = CancellationTokenSource()
    token = cancellation.Token

    def evaluate_js(script, parse_json):
        if token.IsCancellationRequested:
            return None

        def submit():
            # Check on the UI thread, not just before scheduling the call there.
            if not token.IsCancellationRequested and not view.IsDisposed:
                return view.ExecuteScriptAsync(script)
            return None

        try:
            # Invoke on the form rather than the WebView2 control being torn down.
            task = form.Invoke(Func[Object](submit))
            if task is None:
                return None
            # pywebview normally waits for a script continuation that may never
            # run once the view is disposed. Cancel the wait when closing starts.
            task.Wait(token)
            result = json.loads(task.Result)
        except Exception:
            if not token.IsCancellationRequested:
                raise
            return None

        if parse_json and isinstance(result, str):
            try:
                return json.loads(result)
            except ValueError:
                pass
        return result

    def cancel():
        cancellation.Cancel()

    browser.evaluate_js = evaluate_js
    window.events.closing += cancel
