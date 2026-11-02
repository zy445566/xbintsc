# This Might Be the Most Exciting Thing for JS Developers This Year

![3D pelican riding a bicycle](../../examples/gui/pelican-3d/preview.png)

The image above is a 3D pelican riding a bicycle that I built with JS + HTML + WebGL2, and the framerate is excellent too.

You might say, "Isn't that just a matter of having hands and an AI these days?"

But what if I told you that I compiled that very thing into a native binary with xbintsc — under 10 MB, able to run with no WebView environment at all, and able to mix Node.js and HTML in a single file, like this:

```html
<html>
    <body>
    <div id="out">loading...</div>
    <script lang="ts">
        import { readFileSync } from "fs";

        const out = document.getElementById("out");
        const world = readFileSync("data.txt", "utf8");
        out.textContent = "hello " + world;
    </script>
    </body>
</html>
```

Keep in mind that Chrome alone used to take at least 100 MB. The xbintsc build is 10 MB and doesn't need a WebView environment — and that means far more than just a massive reduction in the footprint of desktop development, or a more correct implementation path.

What is truly epoch-making is this: it is now possible for JS developers to run the applications they want to build on embedded devices.

[xbintsc on GitHub](https://github.com/zy445566/xbintsc)
