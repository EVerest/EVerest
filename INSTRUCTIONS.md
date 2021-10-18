# Instructions for using this template

1) Review and update (README.md) and (tsc/README.md) with the [PROJECT NAME] and [PROJECT LOGO], as well as the contents for the various sections.
2) To enable creating meeting agendas from GitHub issues, follow steps 3-5.
3) In the [meeting agenda template](.github/ISSUE_TEMPLATE/meeting.md), update `[PROJECT NAME]` and add the TSC members under the ['Attendance' section](.github/ISSUE_TEMPLATE/meeting.md#attendance).
4) Set the meeting date and time, along with the cadence in the [build agenda workflow](.github/workflows/build_agenda.yml) in the key `schedules:` that specifies a ISO-8601 interval. Examples for repeating:

```
# start 2020-04-22 at 16:00, repeat every 2 weeks
schedules: 2020-04-22T16:00:00.0Z/P2W
# start 2020-04-22 at 16:00, repeat every 7 days
schedules: 2020-06-22T16:00:00.0Z/P7D
```
5) In the [send agenda workflow](.github/workflows/send_agenda.yml), update `[PROJECT NAME]` and `[MAILING LIST EMAIL]`. 
6) Any items with the label `meeting` will be added to the agenda automatically when it is built the day prior to the meeting.
