"""CI test window: logs every mouse/key event it receives (root coordinates) and its text box contents as JSON lines."""
import json, sys, time, tkinter as tk

log = open(sys.argv[1], 'a', buffering=1)
root = tk.Tk()
root.title('MR Test Target')
root.geometry('800x600+100+100')
box = tk.Text(root, height=4, width=60)
box.pack(pady=10)
canvas = tk.Canvas(root, bg='#304860', highlightthickness=0)
canvas.pack(fill='both', expand=True)


def w(kind, e, **kw):
    log.write(json.dumps(dict(t=time.time(), ev=kind, x=e.x_root, y=e.y_root, **kw)) + '\n')


root.bind_all('<ButtonPress>', lambda e: w('down', e, b=e.num))
root.bind_all('<ButtonRelease>', lambda e: w('up', e, b=e.num))
root.bind_all('<KeyPress>', lambda e: w('key', e, k=e.keysym))
root.bind_all('<KeyRelease>', lambda e: log.write(json.dumps(dict(t=time.time(), ev='text', text=box.get('1.0', 'end-1c'))) + '\n'))
root.after(300, lambda: (root.lift(), box.focus_force()))
root.mainloop()
