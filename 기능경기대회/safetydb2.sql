-- MySQL dump 10.13  Distrib 8.0.36, for Linux (x86_64)
--
-- Host: 192.168.0.28    Database: safetydb
-- ------------------------------------------------------
-- Server version	8.0.43-0ubuntu0.24.04.1

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!50503 SET NAMES utf8 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Dumping data for table `admin`
--

LOCK TABLES `admin` WRITE;
/*!40000 ALTER TABLE `admin` DISABLE KEYS */;
INSERT INTO `admin` VALUES ('admin','1234','관리자','최고 관리자','010-0000-0000',1,'admin@example.com');
/*!40000 ALTER TABLE `admin` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Dumping data for table `door_request`
--

LOCK TABLES `door_request` WRITE;
/*!40000 ALTER TABLE `door_request` DISABLE KEYS */;
/*!40000 ALTER TABLE `door_request` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Dumping data for table `employee`
--

LOCK TABLES `employee` WRITE;
/*!40000 ALTER TABLE `employee` DISABLE KEYS */;
INSERT INTO `employee` VALUES (1,'이영진',29,'010-0000-0000','안전기계관리부','주임','정규직',2023,'EMP1-821C-QR',1,0),(2,'유진',31,'010-1234-5678','안전관리팀','사원','정규직',2024,'EMP2-2260-QR',1,0);
/*!40000 ALTER TABLE `employee` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Dumping data for table `equipment`
--

LOCK TABLES `equipment` WRITE;
/*!40000 ALTER TABLE `equipment` DISABLE KEYS */;
/*!40000 ALTER TABLE `equipment` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Dumping data for table `fire_event`
--

LOCK TABLES `fire_event` WRITE;
/*!40000 ALTER TABLE `fire_event` DISABLE KEYS */;
/*!40000 ALTER TABLE `fire_event` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Dumping data for table `gate_check`
--

LOCK TABLES `gate_check` WRITE;
/*!40000 ALTER TABLE `gate_check` DISABLE KEYS */;
INSERT INTO `gate_check` VALUES ('2025-09-12 16:48:05',2,'유진','안전관리팀'),('2025-09-15 12:16:47',2,'유진','안전관리팀'),('2025-09-15 15:08:51',2,'유진','안전관리팀'),('2025-09-15 15:08:53',2,'유진','안전관리팀'),('2025-09-15 15:08:53',2,'유진','안전관리팀'),('2025-09-15 15:09:35',2,'유진','안전관리팀'),('2025-09-15 15:09:36',2,'유진','안전관리팀'),('2025-09-15 15:09:36',2,'유진','안전관리팀'),('2025-09-15 15:18:10',2,'유진','안전관리팀'),('2025-09-15 15:18:10',2,'유진','안전관리팀'),('2025-09-15 15:18:10',2,'유진','안전관리팀'),('2025-09-15 15:19:17',1,'이영진','안전기계관리부'),('2025-09-15 15:19:17',1,'이영진','안전기계관리부'),('2025-09-15 15:20:04',1,'이영진','안전기계관리부'),('2025-09-15 15:20:04',1,'이영진','안전기계관리부'),('2025-09-15 15:20:04',1,'이영진','안전기계관리부'),('2025-09-15 15:22:01',2,'유진','안전관리팀'),('2025-09-15 15:22:01',2,'유진','안전관리팀'),('2025-09-15 15:22:01',2,'유진','안전관리팀'),('2025-09-15 15:23:00',2,'유진','안전관리팀'),('2025-09-15 15:23:00',2,'유진','안전관리팀'),('2025-09-15 15:23:01',2,'유진','안전관리팀'),('2025-09-15 15:23:01',2,'유진','안전관리팀'),('2025-09-15 16:13:00',2,'유진','안전관리팀'),('2025-09-15 16:13:10',2,'유진','안전관리팀'),('2025-09-15 16:13:11',2,'유진','안전관리팀'),('2025-09-15 16:13:29',2,'유진','안전관리팀'),('2025-09-15 16:13:29',2,'유진','안전관리팀'),('2025-09-15 16:13:35',2,'유진','안전관리팀'),('2025-09-15 16:17:59',2,'유진','안전관리팀'),('2025-09-15 16:18:03',2,'유진','안전관리팀'),('2025-09-15 16:51:32',2,'유진','안전관리팀'),('2025-09-15 17:01:44',2,'유진','안전관리팀'),('2025-09-15 17:10:47',2,'유진','안전관리팀'),('2025-09-15 18:03:20',2,'유진','안전관리팀'),('2025-09-15 18:29:44',2,'유진','안전관리팀'),('2025-09-15 18:29:46',2,'유진','안전관리팀'),('2025-09-15 19:17:35',2,'유진','안전관리팀'),('2025-09-15 19:17:35',2,'유진','안전관리팀'),('2025-09-15 19:17:36',2,'유진','안전관리팀'),('2025-09-15 19:17:36',2,'유진','안전관리팀'),('2025-09-15 19:17:36',2,'유진','안전관리팀'),('2025-09-15 19:17:37',2,'유진','안전관리팀'),('2025-09-15 19:17:37',2,'유진','안전관리팀'),('2025-09-15 19:17:37',2,'유진','안전관리팀'),('2025-09-15 19:17:37',2,'유진','안전관리팀'),('2025-09-15 19:49:18',2,'유진','안전관리팀'),('2025-09-15 19:49:54',2,'유진','안전관리팀'),('2025-09-15 19:50:33',2,'유진','안전관리팀'),('2025-09-16 09:22:00',2,'유진','안전관리팀'),('2025-09-16 09:41:51',2,'유진','안전관리팀'),('2025-09-16 09:41:52',2,'유진','안전관리팀'),('2025-09-16 09:41:53',2,'유진','안전관리팀'),('2025-09-16 09:43:13',1,'이영진','안전기계관리부'),('2025-09-16 09:43:13',1,'이영진','안전기계관리부'),('2025-09-16 09:43:13',1,'이영진','안전기계관리부');
/*!40000 ALTER TABLE `gate_check` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Dumping data for table `robot_info`
--

LOCK TABLES `robot_info` WRITE;
/*!40000 ALTER TABLE `robot_info` DISABLE KEYS */;
INSERT INTO `robot_info` VALUES (1,'2025-09-12 10:00:04','/home/bready/Videos/20250912_095937_zone-A.avi','/home/lms7/Uploads/20250912_095937_zone-A.avi'),(2,'2025-09-12 10:42:50','/home/bready/Videos/20250912_104213_zone-A.avi','/home/lms7/Uploads/20250912_104213_zone-A.avi'),(3,'2025-09-12 17:18:29','/home/bready/Videos/20250912_171815_zone-A.avi','/home/lms7/Uploads/20250912_171815_zone-A.avi'),(4,'2025-09-13 14:53:18','/home/bready/Videos/20250913_145300_zone-A.avi','/home/lms7/Uploads/20250913_145300_zone-A.avi'),(5,'2025-09-13 14:56:02','/home/bready/Videos/20250913_145546_zone-A.avi','/home/lms7/Uploads/20250913_145546_zone-A.avi'),(6,'2025-09-13 15:35:58','/home/bready/Videos/20250913_153530_zone-A.avi','/home/user1/Uploads/20250913_153530_zone-A.avi'),(7,'2025-09-13 16:29:13',NULL,NULL),(8,'2025-09-13 16:53:22',NULL,NULL),(9,'2025-09-13 16:59:26',NULL,NULL),(10,'2025-09-13 17:12:32','/home/bready/Videos/20250913_171334_zone-A.avi','/home/user1/Uploads/20250913_171334_zone-A.avi'),(11,'2025-09-13 17:16:21','/home/bready/Videos/20250913_171609_zone-A.avi','/home/user1/Uploads/20250913_171609_zone-A.avi'),(12,'2025-09-13 17:52:28','/home/bready/Videos/20250913_175216_zone-A.avi','/home/user1/Uploads/20250913_175216_zone-A.avi'),(13,'2025-09-13 17:55:57','/home/bready/Videos/20250913_175723_zone-A.avi','/home/user1/Uploads/20250913_175723_zone-A.avi'),(14,'2025-09-13 17:59:06','/home/bready/Videos/20250913_180023_zone-A.avi','/home/user1/Uploads/20250913_180023_zone-A.avi'),(15,'2025-09-13 18:01:10','/home/bready/Videos/20250913_180228_zone-A.avi','/home/user1/Uploads/20250913_180228_zone-A.avi'),(16,'2025-09-13 18:04:18','/home/bready/Videos/20250913_180359_zone-A.avi','/home/user1/Uploads/20250913_180359_zone-A.avi'),(17,'2025-09-15 14:40:57','/home/bready/Videos/20250915_144241_zone-A.avi','/home/lms7/Uploads/20250915_144241_zone-A.avi'),(18,'2025-09-15 14:44:20',NULL,NULL),(19,'2025-09-15 15:37:55','/home/bready/Videos/20250915_153744_zone-A.avi','/home/sms/Uploads/20250915_153744_zone-A.avi'),(20,'2025-09-15 15:41:45','/home/bready/Videos/20250915_154115_zone-A.avi','/home/sms/Uploads/20250915_154115_zone-A.avi'),(21,'2025-09-15 15:43:47','/home/bready/Videos/20250915_154338_zone-A.avi',NULL),(22,'2025-09-15 15:55:48','/home/bready/Videos/20250915_155537_zone-A.avi','/home/sms/Uploads/20250915_155537_zone-A.avi'),(23,'2025-09-15 16:50:37','/home/bready/Videos/20250915_165000_zone-A.avi','/home/iut-125/Uploads/20250915_165000_zone-A.avi'),(24,'2025-09-15 17:34:14','/home/bready/Videos/20250915_173332_zone-A.avi','/home/iut-125/Uploads/20250915_173332_zone-A.avi'),(25,'2025-09-15 17:52:40',NULL,NULL),(26,'2025-09-15 17:55:41',NULL,NULL),(27,'2025-09-15 19:18:29','/home/bready/Videos/20250915_191819_zone-A.avi','/home/iut-125/Uploads/20250915_191819_zone-A.avi'),(28,'2025-09-15 19:26:36','/home/bready/Videos/20250915_192624_zone-A.avi','/home/iut-125/Uploads/20250915_192624_zone-A.avi'),(29,'2025-09-15 19:31:39','/home/bready/Videos/20250915_193130_zone-A.avi','/home/iut-125/Uploads/20250915_193130_zone-A.avi');
/*!40000 ALTER TABLE `robot_info` ENABLE KEYS */;
UNLOCK TABLES;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

-- Dump completed on 2025-09-16  9:47:37
