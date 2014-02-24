/*
 * Copyright (C) 2014 Thomas Jost
 * This file is part of spop and is Licensed under the GPLv3. See COPYING for
 * details.
 */

define(["backbone", "underscore", "backbone.epoxy"],
function(Backbone,   _) {
    "use strict";

    var StatusView = Backbone.Epoxy.View.extend({
        el: "#status",
        events: {
            "click a.play": "play",
            "click a.stop": "stop",
            "click a.prev": "prev",
            "click a.next": "next"
        },
        computeds: {
            img_src: function() {
                return "data:image/jpeg;base64," + this.getBinding("img");
            },
            proxy_shuffle: {
                deps: ["shuffle"],
                get: function(shuffle) { return shuffle; },
                set: function() { this.model.toggle_shuffle(); }
            },
            proxy_repeat: {
                deps: ["repeat"],
                get: function(repeat) { return repeat; },
                set: function() { this.model.toggle_repeat(); }
            },
            stopped: function() { return this.getBinding("status") === "stopped"; },
            playing: function() { return this.getBinding("status") === "playing"; }
        },

        initialize: function() {
            this.model.on("change", this.update_page_title, this);
            this.update_page_title(this.model);
        },

        update_page_title: function(model) {
            var page_title = "",
                status = model.get("status"),
                artist = model.get("artist"),
                title = model.get("title");
            if (status === "playing")
                page_title = "▶ ";
            if (artist && title)
                page_title += title + " (" + artist + ") - ";
            page_title += "Spop";
            document.title = page_title;
        },

        play: function() { this.model.play(); },
        stop: function() { this.model.stop(); },
        prev: function() { this.model.prev(); },
        next: function() { this.model.next(); }
    });

    return StatusView;
});
