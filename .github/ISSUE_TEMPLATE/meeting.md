# EVerest TSC Meeting - <%= date.toFormat("MMMM d, yyyy") %>

## Date/Time

| Timezone | Date/Time |
|----------|-----------|
<%= [
  'America/Los_Angeles',
  'America/Denver',
  'America/Chicago',
  'America/New_York',
  'Europe/London',
  'Europe/Berlin',
  'Asia/Shanghai',
  'Asia/Tokyo',
].map((zone) => {
  return `| ${zone} | ${date.setZone(zone).toFormat('EEE dd-MMM-yyyy HH:mm (hh:mm a)')} |`
}).join('\n') %>

Or in your local time:
* https://www.timeanddate.com/worldclock/?iso=<%= date.toFormat("yyyy-MM-dd'T'HH:mm:ss") %>

All meetings are listed on the project calendar at https://lists.lfenergy.org/calendar, subject to the mailing lists you are subscribed to.

### Conference call details

_fill in Zoom details_

### Meeting recordings

_fill in recording details_

## Attendance

### Voting member rollcall:
- [ ] marco.moeller@pionix.de
- [ ] cornelius.claussen@pionix.de

_fill in voting members as a checklist_

### Other Attendees


## Agenda

Extracted from **<%= agendaLabel %>** labelled issues and pull requests from **<%= owner %>/<%= repo %>** prior to the meeting.

<%= agendaIssues.map((i) => {
  return `* ${i.title} [#${i.number}](${i.html_url})`
}).join('\n') %>

## Action Items


## Notes
