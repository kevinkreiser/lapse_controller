[![Build Status](https://travis-ci.org/kevinkreiser/lapse_controller.svg?branch=master)](https://travis-ci.org/kevinkreiser/lapse_controller)

Lapse Controller is two things. First, it is an application that talks to a network of time lapse cameras, downloading imagery from them at regular intervals so they don't fill up. Second, its a front end application that allows you to monitor what time lapse cameras are connected and change their capturing schedule with a simple website like this:

![Front End Interface](docs/controller.png)

Yes there are tons of secuirty holes here. Look for issues and thoughts on resolving some of them at a future point in time.

Making A Video
--------------

So you did a capture and now you have 10k images lying around. You might be wondering how you can then turn these into a video. For a single day, say November 2nd 2015, you can create a video like this:

    ffmpeg -pattern_type glob -i './lapse_controller/a48eb3c348b4ce80/sdcard/lapse/photos/2015_11_02/*/*.JPG' -vf scale=iw/2:ih/2 -r 30 output.mp4
    
In fact that is what I did for the construction of our new house:eYc31Whf5Vk

[![Maiden Time-Lapse Sequence](http://img.youtube.com/vi/eYc31Whf5Vk/0.jpg)](http://www.youtube.com/watch?v=eYc31Whf5Vk)

