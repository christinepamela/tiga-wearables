# TIGA Design Philosophy

This document describes the design principles that guide every decision in the TIGA project — the wearable, the companion app, and any future variants. It is the answer to the question: *"how do we know if a design choice is right?"*

It is not a style guide (those live in code: `--accent`, `--surface`, etc.). It is the reasoning behind the style.

---

## Who we design for

TIGA is for **people whose health needs more attention than a typical adult**. This is age-correlated but not age-defined. In practice, our primary user is:

- **Adults aged 55 and above**, where chronic conditions, fall risk, medication adherence, and the desire to share peace of mind with family become central concerns.
- **Anyone who needs caretaking regardless of age** — someone recovering from a stroke, living with early-onset cognitive decline, going through cancer treatment, post-surgery recovery, or managing a chronic illness that requires monitoring.

We do not target the wellness-and-fitness-tracker market (Apple Watch, Garmin, Fitbit). Those products serve healthy people optimizing performance. TIGA serves people for whom health is a real daily concern.

By the 60s and 70s, most users will have at least one chronic condition. By the 80s, the focus shifts toward fall prevention, medication adherence, and staying connected with family. Our design must serve all of these without losing the dignity of the user.

We resist the urge to design *only* for the most fragile user. That would mean dumbing things down for everyone else. Instead we follow:

> **Simple by default, deep when invited.**

The Home screen looks calm and uncluttered for an 80-year-old who just wants reassurance everything is fine. The same screen has expandable sections, advanced metrics, and customization for a 60-year-old who survived a heart attack and now wants to track every detail of recovery. Same app, different depths.

The secondary user is a **caregiver** — typically an adult child, spouse, or professional carer looking after a TIGA wearer. Their needs are different (binary status, peace of mind, no raw medical data), so the app has a separate caregiver mode.

---

## What we want users to feel

When someone uses TIGA — opens the app, glances at the watch, gets a notification — these are the emotions we are trying to invoke, in order of importance:

### 1. Capable
Users have lived 50, 60, 70+ years. They have raised families, run businesses, managed complex lives. The app must treat them as competent adults. We never:

- Talk down to them
- Hide controls "for their own good"
- Use childish language or imagery
- Assume they cannot handle their own data

We always:

- Use proper medical terminology *with* a plain-English explanation
- Show numbers and ranges, not just colors
- Offer the option to dive deeper if they want

### 2. Calm
Health tracking is anxiety-inducing by default. Heart rates fluctuate. SpO2 dips. Steps fall short. A bad-faith design would alarm the user every time something is off. We do the opposite.

The app is **quiet by default**. If everything is fine, nothing demands attention. If something needs checking, it surfaces gently — never with red alerts unless it is a true emergency (fall, SOS).

The default emotional baseline is *reassurance*: "everything is okay, here is your day."

### 3. Connected
Many older adults feel forgotten. The app reminds them they matter — to family, to their doctor, to themselves. This shows up in:

- Personalized greetings using their name
- The caregiver receiving "all is well" updates without any action required
- The doctor's report compiling their data into something useful
- Optional links to community tools (like Bitchat for free chat with neighbors)

### 4. Trusted
We do not treat users like they will break the app. We do not gate features behind "are you sure?" prompts unless there is real risk. We expose advanced data because some users want it. We let them turn things off.

Trust runs both ways. The app trusts the user. The user trusts the app to be honest about what it knows and what it does not.

### 5. Joyful
Small moments of warmth, beauty, or delight. A nice quote on the watch when it powers on for the first time of the day. A pretty walk map. A streak that celebrates consistency. A watch face that is genuinely beautiful — not a generic digital readout.

These moments are sparse on purpose. Joy is a seasoning, not a main course.

---

## What we deliberately avoid

### Medical-tracker coldness
Red alerts. "Critical" labels. Percentages without context. Charts with downward arrows. This is the aesthetic of clinical anxiety. We use it only when truly warranted (an actual fall, an actual SOS).

### Fitness-app aggression
Calorie counts. "You crushed it!" Streaks that shame missed days. Leaderboards. Competitive language. These work for 25-year-olds chasing PRs. They alienate everyone else.

### Childishness
Cartoon graphics. Oversimplified language. Googly emojis everywhere. Patronizing copy ("Great job!" for taking a pill). Older adults find this insulting, and rightly so.

### Information density
Too many numbers, charts, and labels on one screen. We embrace whitespace. Each screen has one or two primary jobs. Everything else is on a tap or scroll away.

### Bright color competition
Many apps use 5+ colors fighting for attention. TIGA uses a calm palette: dark gray base, single green accent, occasional muted warning orange or red. Color carries meaning — when something is red, it actually matters.

---

## Voice and tone

### Greetings
Personalized with the user's name when set. Time-of-day aware. Occasional warmth on special moments.

```
Default morning:    "Good morning, Agnes"
Sunday:             "Happy Sunday, Agnes"
After 7-day streak: "Wonderful, Agnes"
Birthday:           "Happy birthday, Agnes"
Late night (rare):  "Late hours, Agnes"
```

### Status descriptions
Always lead with a positive interpretation if true.

```
Heart rate 72 bpm: "Steady and healthy"  (not "72 bpm")
Steps 2,400 / 3,000: "Good start — keep going"  (not "20% to goal")
Battery 84%: "~17h remaining"  (not "Battery normal")
```

### Notifications and alerts
Plain language, never alarming unless truly urgent.

```
Soft:    "Time for your morning medication"
Medium:  "Heart rate higher than usual — take a moment to rest"
Urgent:  "Fall detected — sending alert in 30 seconds"
```

### Errors and edge cases
Never blame the user. Frame as a gentle suggestion.

```
Bad:   "Connection failed. Retry."
Good:  "Trouble connecting to your watch. Make sure it is nearby and turned on."

Bad:   "Invalid input."
Good:  "Please enter a number between 20 and 600."
```

### Empty states
Encouraging, never empty-feeling.

```
Bad:   "No walks recorded yet"
Good:  "Your first walk is waiting. Tap Start when you are ready."
```

---

## Visual principles

### Color
- **Background**: deep neutral dark (#0a0a0a). Easier on aging eyes than pure black or bright white.
- **Accent**: warm green (#4ade80). Used sparingly for primary actions and "all is well" status.
- **Muted text**: medium gray (#888) for secondary information.
- **Warning**: orange (#fb923c). Used only when attention is needed but not urgent.
- **Danger**: red (#ef4444). Used only for true emergencies.

The palette is intentionally restrained. If everything is highlighted, nothing is.

### Typography
- System font stack (San Francisco on iOS, Roboto on Android, Segoe UI on Windows). Renders best on each platform's native rendering engine.
- Base size 16px — comfortable for 50-year-old eyes without being patronizingly large.
- Important numbers (heart rate, steps) are large (28-52px) and bold for at-a-glance reading.
- Labels and metadata are small (11-13px) and muted.
- Line height 1.4-1.5 for body text. Density of medical apps is uncomfortable.

### Spacing
- Generous padding inside cards (16-20px).
- Generous gaps between cards (10-12px).
- The app should feel "breathy," not packed.

### Touch targets
- Minimum 44px tall (Apple HIG standard).
- Buttons full-width when stacked, half-width when paired.
- Toggle switches large enough that arthritic fingers can hit them.

### Motion
- Subtle, brief, calming.
- Heartbeat pulse on the heart icon (gentle scale animation).
- Slide-up modals for inputs.
- No bouncy springs. No flashy transitions. We are not entertaining the user, we are quietly serving them.

---

## Layout principles

### The Home screen is the most important screen
Most users will only ever look at this screen. Design it so that:

1. Within 2 seconds, the user knows their main metrics are fine (or what needs attention).
2. The most common action (start a walk, log info) is one tap away.
3. Nothing alarming greets them by default.
4. Everything beyond the basics is accessible by tapping into a section.

### Progressive disclosure
We show defaults to most users. Power users can expand for more.

```
Walk in progress (default): Time, Steps, Distance, Heart rate
                            [More stats →]
                            ↓
                            (tapped) → Pace, Elevation, Gait cadence,
                                       Cadence, Symmetry, etc.
```

This serves the 80-year-old (simple) and the 65-year-old fitness enthusiast (deep) without forcing either to compromise.

### Settings is for setup, not daily use
Settings holds configuration that the user does once: name, conditions, contacts, watch face choice. Daily activities (logging, walking, checking stats) all happen on Home or via the bottom nav.

If a user has to visit Settings every day, we have failed at design.

---

## The wearable's design philosophy

The watch is a different product from the app. The app is a tool — for daily check-ins, doctor reports, caregiver updates. The watch is a *companion* — worn all day, glanced at often, present in the user's life.

The watch's design must:

### 1. Look like a watch, not a gadget
Most "fitness watches" look like gadgets. They are obvious. They do not blend in. TIGA's watch face is a beautifully rendered analog watch — chosen by the user from a library of designs (classic, sport, art, festive, custom). It happens to have lifesaving sensors inside, but it does not advertise that fact.

### 2. Be quiet most of the time
Always-on dim mode. Wakes to full brightness on raise-to-wake. Shows watch face by default. Other information accessible via button or tap (piezo).

### 3. Surface info gracefully
A tap shows steps and HR for 5 seconds, then returns to watch face. A long press starts SOS confirmation. A button press cycles through info screens. Information is *available*, not *imposed*.

### 4. Be giftable
This is the soul of the watch. A daughter giving her mother a TIGA is giving more than a health tracker. She is giving an object that is beautiful in itself, that her mother can wear with pride, that has meaning. The watch face system makes this possible.

See `WATCH-FACES.md` for the technical spec.

---

## How to use this document

When making any design decision, ask:

1. Does this make the user feel **capable, calm, connected, trusted, or joyful**?
2. Does it serve someone who **needs simple reassurance** without alienating someone who **wants deep detail**?
3. Is it **quiet by default**, with **depth on demand**?
4. Would I be **proud to give this to my own mother (or to someone recovering from a serious illness)**?

If yes to all, ship it. If no, rework it.

If unsure, build it and look at it for a day. Often the answer becomes obvious in the morning.

---

*This document is a living spec. Update it when our understanding deepens or our principles refine. Keep the changelog at the bottom.*

## Changelog
- 2026 build session — initial draft after extensive design conversations on age range, watch faces, voice, layout
- 2026 update — refined "who we design for" to be needs-based not age-based. Primary user is 55+ OR anyone needing caretaking. Removed fitness-tracker market language.
- 2026 build session — initial draft after extensive design conversations on age range, watch faces, voice, layout
