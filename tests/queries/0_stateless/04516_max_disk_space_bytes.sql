-- Tags: no-fasttest
SELECT total_space <= 10485760, free_space <= 10485760
FROM system.disks
WHERE name = 'local_max_disk_space_bytes';
